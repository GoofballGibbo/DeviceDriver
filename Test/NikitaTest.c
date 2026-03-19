static int proc_state_show(struct seq_file *m, void *v) {
    seq_printf(m, driver_state==STATE_IDLE?"IDLE\n":
                  driver_state==STATE_RUNNING?"RUNNING\n":"COMPLETE\n");
    return 0;
}
static int proc_stats_show(struct seq_file *m, void *v) {
    unsigned long flags; ktime_t now; s64 em; int cw,mw,cc,mc,wpm,raw_wpm;
    spin_lock_irqsave(&wpm_lock,flags);
    cw=correct_words; mw=missed_words; cc=correct_chars; mc=missed_chars;
    now=ktime_get(); em=ktime_to_ms(ktime_sub(now,test_start));
    spin_unlock_irqrestore(&wpm_lock,flags);
    wpm=em>0?(int)((cw*60000LL)/em):0;
    raw_wpm=em>0?(int)(((cw+mw)*60000LL)/em):0;
    int tw=cw+mw, tc=cc+mc;
    seq_printf(m,"state:           %s\n",
               driver_state==STATE_IDLE?"IDLE":
               driver_state==STATE_RUNNING?"RUNNING":"COMPLETE");
    seq_printf(m,"wpm:             %d\n",wpm);
    seq_printf(m,"raw_wpm:         %d\n",raw_wpm);
    seq_printf(m,"word_accuracy:   %d%%\n",tw>0?(cw*100)/tw:0);
    seq_printf(m,"char_accuracy:   %d%%\n",tc>0?(cc*100)/tc:0);
    seq_printf(m,"correct_words:   %d\n",cw);
    seq_printf(m,"missed_words:    %d\n",mw);
    seq_printf(m,"correct_chars:   %d\n",cc);
    seq_printf(m,"missed_chars:    %d\n",mc);
    seq_printf(m,"elapsed_seconds: %lld\n",em/1000);
    return 0;
}
static int proc_errors_show(struct seq_file *m, void *v) {
    unsigned long flags; int i, top; struct word_entry local[INCORRECT_MAX];
    spin_lock_irqsave(&wpm_lock,flags);
    top=incorrect_top;
    memcpy(local,incorrect_stack,top*sizeof(struct word_entry));
    spin_unlock_irqrestore(&wpm_lock,flags);
    for (i=0; i<top; i++)
        seq_printf(m,"%d:%d\n",local[i].start_index,local[i].end_index);
    return 0;
}
static int proc_state_open(struct inode *i,struct file *f)
    { return single_open(f,proc_state_show,NULL); }
static int proc_stats_open(struct inode *i,struct file *f)
    { return single_open(f,proc_stats_show,NULL); }
static int proc_errors_open(struct inode *i,struct file *f)
    { return single_open(f,proc_errors_show,NULL); }

static const struct proc_ops proc_state_fops  = {.proc_open=proc_state_open, .proc_read=seq_read,.proc_lseek=seq_lseek,.proc_release=single_release};
static const struct proc_ops proc_stats_fops  = {.proc_open=proc_stats_open, .proc_read=seq_read,.proc_lseek=seq_lseek,.proc_release=single_release};
static const struct proc_ops proc_errors_fops = {.proc_open=proc_errors_open,.proc_read=seq_read,.proc_lseek=seq_lseek,.proc_release=single_release};
