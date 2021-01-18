/*
 * jiq.c -- the just-in-queue module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */
 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>     /* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>  /* error codes */
#include <linux/workqueue.h>
#include <linux/preempt.h>
#include <linux/interrupt.h> /* tasklets */
#include <linux/seq_file.h>

MODULE_LICENSE("Dual BSD/GPL");

/*
 * The delay for the delayed workqueue timer file.
 */
static long delay = 1;
module_param(delay, long, 0);

struct proc_dir_entry *jiq_proc_dir;

/*
 * This module is a silly one: it only embeds short code fragments
 * that show how enqueued tasks `feel' the environment
 */

#define LIMIT	(PAGE_SIZE-128)	/* don't print any more after this size */

/*
 * Print information about the current environment. This is called from
 * within the task queues. If the limit is reched, awake the reading
 * process.
 */
static DECLARE_WAIT_QUEUE_HEAD (jiq_wait);

/*
 * Keep track of info we need between task queue runs.
 */
static struct clientdata {
	struct work_struct jiq_work;
	struct delayed_work jiq_delayed_work;
	struct timer_list timer;
	int len;
	//char *buf;
	struct seq_file *seq;
	unsigned long jiffies;
	long delay;
} jiq_data;

#define SCHEDULER_QUEUE ((task_queue *) 1)



static void jiq_print_tasklet(unsigned long);
static DECLARE_TASKLET(jiq_tasklet, jiq_print_tasklet, (unsigned long)&jiq_data);


/*
 * Do the printing; return non-zero if the task should be rescheduled.
 */
static int jiq_print(void *ptr)
{
	struct clientdata *data = ptr;
	int len = data->len;
	//char *buf = data->buf;
	unsigned long j = jiffies;

	if (len > LIMIT) { 
		wake_up_interruptible(&jiq_wait);
		return 0;
	}

	/*
	if (len == 0)
		len = sprintf(buf,"    time  delta preempt   pid cpu command\n");
	else
		len = 0;
	*/
	if (len == 0)
		seq_printf(data->seq, "    time  delta preempt   pid cpu command\n");
	

  	/* intr_count is only exported since 1.3.5, but 1.99.4 is needed anyways 
	len += sprintf(buf+len, "%9li  %4li     %3i %5i %3i %s\n",
			j, j - data->jiffies,
			preempt_count(), current->pid, smp_processor_id(),
			current->comm);
	*/
	seq_printf(data->seq, "%9li  %4li     %3i %5i %3i %s\n",
			j, j - data->jiffies,
			preempt_count(), current->pid, smp_processor_id(),
			current->comm);

	//data->len += len;
	data->len = 1;	// not first time
	//data->buf += len;
	data->jiffies = j;
	return 1;
}


/*
 * Call jiq_print from a work queue
 */
static void jiq_print_wq(struct work_struct *work)
{
	struct clientdata *data = container_of(work, struct clientdata, jiq_work);
    
	if (! jiq_print(data))
		return;

	schedule_work(&jiq_data.jiq_work);
}

static void jiq_print_wq_delayed(struct work_struct *work)
{
	struct clientdata *data = container_of(work, struct clientdata, jiq_delayed_work.work);
    
	if (! jiq_print(data))
		return;
    
	schedule_delayed_work(&jiq_data.jiq_delayed_work, data->delay);
}

/*
static int jiq_read_wq(char *buf, char **start, off_t offset,
                   int len, int *eof, void *data)
		   */
static int jiq_read_wq(struct seq_file *seq, void *data)
{
	DEFINE_WAIT(wait);
	
	jiq_data.len = 0;                /* nothing printed, yet */
	//jiq_data.buf = buf;              /* print in this place */
	jiq_data.seq = seq;              /* print in this place */
	jiq_data.jiffies = jiffies;      /* initial time */
	jiq_data.delay = 0;
    
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_work(&jiq_data.jiq_work);
	schedule();
	finish_wait(&jiq_wait, &wait);

	//*eof = 1;
	//return jiq_data.len;
	return 0;
}


/*
static int jiq_read_wq_delayed(char *buf, char **start, off_t offset,
                   int len, int *eof, void *data)
*/
static int jiq_read_wq_delayed(struct seq_file *seq, void *data)
{
	DEFINE_WAIT(wait);
	
	jiq_data.len = 0;                /* nothing printed, yet */
	//jiq_data.buf = buf;              /* print in this place */
	jiq_data.seq = seq;
	jiq_data.jiffies = jiffies;      /* initial time */
	jiq_data.delay = delay;
    
	prepare_to_wait(&jiq_wait, &wait, TASK_INTERRUPTIBLE);
	schedule_delayed_work(&jiq_data.jiq_delayed_work, delay);
	schedule();
	finish_wait(&jiq_wait, &wait);

	//*eof = 1;
	//return jiq_data.len;
	return 0;
}




/*
 * Call jiq_print from a tasklet
 */
static void jiq_print_tasklet(unsigned long ptr)
{
	if (jiq_print ((void *) ptr))
		tasklet_schedule (&jiq_tasklet);
}



/*
static int jiq_read_tasklet(char *buf, char **start, off_t offset, int len,
                int *eof, void *data)
*/
static int jiq_read_tasklet(struct seq_file *seq, void *data)
{
	jiq_data.len = 0;                /* nothing printed, yet */
	//jiq_data.buf = buf;              /* print in this place */
	jiq_data.seq = seq;
	jiq_data.jiffies = jiffies;      /* initial time */

	tasklet_schedule(&jiq_tasklet);
	wait_event_interruptible(jiq_wait, 0);
	//interruptible_sleep_on(&jiq_wait);    /* sleep till completion */

	//*eof = 1;
	//return jiq_data.len;
	return 0;
}




/*
 * This one, instead, tests out the timers.
 */

//static struct timer_list jiq_timer;

static void jiq_timedout(struct timer_list *tmr)
//static void jiq_timedout(unsigned long ptr)
{
	struct clientdata *data = container_of(tmr, struct clientdata, timer);
	jiq_print(data);
	//jiq_print((void *)ptr);            /* print a line */
	wake_up_interruptible(&jiq_wait);  /* awake the process */
}


/*
static int jiq_read_run_timer(char *buf, char **start, off_t offset,
                   int len, int *eof, void *data)
*/
static int jiq_read_run_timer(struct seq_file *seq, void *data)
{

	struct timer_list *jiq_timer = &jiq_data.timer;

	jiq_data.len = 0;           /* prepare the argument for jiq_print() */
	//jiq_data.buf = buf;
	jiq_data.seq = seq;

	jiq_data.jiffies = jiffies;

	timer_setup(jiq_timer, jiq_timedout, 0);
	//init_timer(&jiq_timer);              /* init the timer structure */
	jiq_timer->function = jiq_timedout;
	//jiq_time->data = (unsigned long)&jiq_data;
	jiq_timer->expires = jiffies + HZ; /* one second */

	jiq_print(&jiq_data);   /* print and go to sleep */
	add_timer(jiq_timer);
	wait_event_interruptible(jiq_wait, 0);
	//interruptible_sleep_on(&jiq_wait);  /* RACE */
	del_timer_sync(jiq_timer);  /* in case a signal woke us up */
    
	//*eof = 1;
	//return jiq_data.len;
	return 0;
}



/*
 * the init/clean material
 */

static int jiq_init(void)
{

	/* this line is in jiq_init() */
        INIT_WORK(&jiq_data.jiq_work, jiq_print_wq);
        INIT_DELAYED_WORK(&jiq_data.jiq_delayed_work, jiq_print_wq_delayed);

	jiq_proc_dir = proc_mkdir("jiq", NULL);


	proc_create_single_data("jiqwq", 0, jiq_proc_dir, jiq_read_wq, NULL);
	proc_create_single_data("jiqwqdelay", 0, jiq_proc_dir, jiq_read_wq_delayed, NULL);
	proc_create_single_data("jiqtimer", 0, jiq_proc_dir, jiq_read_run_timer, NULL);
	proc_create_single_data("jiqtasklet", 0, jiq_proc_dir, jiq_read_tasklet, NULL);

	return 0; /* succeed */
}

static void jiq_cleanup(void)
{
	remove_proc_entry("jiqwq", jiq_proc_dir);
	remove_proc_entry("jiqwqdelay", jiq_proc_dir);
	remove_proc_entry("jiqtimer", jiq_proc_dir);
	remove_proc_entry("jiqtasklet", jiq_proc_dir);
}


module_init(jiq_init);
module_exit(jiq_cleanup);
