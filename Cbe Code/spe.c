// program for executing the ocr operations on a thread created for an SPU
#include <spu_mfcio.h>
#include <malloc.h>
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#ifdef HAVE_GETTIMEOFDAY 
#include <sys/time.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "pnm.h"
#include "pgm2asc.h"
#include "pcx.h"
#include "ocr0.h"		/* only_numbers */
#include "progress.h"
#include "version.h"

/* subject of change, we need more output for XML (ToDo) */
void print_output(job_t *job) {
  int linecounter = 0;
  const char *line;

  assert(job);
        
/* TODO: replace getTextLine-loop(line) by output_text() (libs have to use pipes)
   simplify code 2010-09-26
*/
  linecounter = 0;
  line = getTextLine(&(job->res.linelist), linecounter++);
  while (line) {
    /* notice: decode() is shiftet to getTextLine since 0.38 */
    fputs(line, stdout);
    if (job->cfg.out_format==HTML) fputs("<br />",stdout);
    if (job->cfg.out_format!=XML)  fputc('\n', stdout);
    line = getTextLine(&(job->res.linelist), linecounter++);
  }
  free_textlines(&(job->res.linelist));
}

static void mark_end(job_t *job) {
  assert(job);

#ifdef HAVE_GETTIMEOFDAY
  /* show elapsed time */
  if (job->cfg.verbose) {
    struct timeval end, result;
    gettimeofday(&end, NULL);
    timeval_subtract(&result, &end, &job->tmp.init_time);
    fprintf(stderr,"Elapsed time: %d:%02d:%3.3f.\n", (int)result.tv_sec/60,
	(int)result.tv_sec%60, (float)result.tv_usec/1000);
  }
#endif
}


int main(unsigned long long speid) {
 // read SPU id using mailbox
  unsigned int spu_id = spu_read_in_mbox();
  printf ("\n Hello world ! SPU %llx %d\n",speid,spu_id);
  pgm2asc(job);
  mark_end(job);
  print_output(job);
  job_free_image(job)
  return 0;
}
