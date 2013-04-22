// Program for running the GCOR on CBE using ppe threads calling SPE's threads

#include <errno.h>
#include <libspe2.h> // for caling SPE's threads
#include <pthread.h> // for creating PPE's threads
/*requires header files of GOCR for provideing access to GOCR modified code*/
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

job_t *OCR_JOB;

extern spe_program_handle_t ocr_spu; // spu compiled program to be loaded in to Spu created threads

#define MAX_SPU_THREADS 6 // maximum number of spe's available in PS3

pthread_t tid[MAX_SPU_THREADS]; // for creating the threads for ppu


static void out_version(int v) {
  fprintf(stderr, " Optical Character Recognition --- gocr "
          version_string " " release_string "\n"
          " Copyright (C) 2001-2010 Joerg Schulenburg  GPG=1024D/53BDFBE3\n"
          " released under the GNU General Public License\n"
          "Modified by IIITB-students Os Project 10b for cell broadband engine\n"
          "For source Code Please Follow the link below \n"
          "https://sourceforge.net/projects/devocroncbe/ \n");
  /* as recommended, (c) and license should be part of the binary */
  /* no email because of SPAM, see README for contacting the author */
  if (v)
    fprintf(stderr, " use option -h for help\n");
  if (v & 2)
    exit(1);
  return;
}

static void help(void) {
  out_version(0);
  /* output is shortened to essentials, see manual page for details */
  fprintf(stderr,
	  " using: gocr [options] pnm_file_name  # use - for stdin\n"
	  " options (see gocr manual pages for more details):\n"
	  " -h, --help\n"
	  " -i name   - input image file (pnm,pgm,pbm,ppm,pcx,...)\n"
	  " -o name   - output file  (redirection of stdout)\n"
	  " -e name   - logging file (redirection of stderr)\n"
	  " -x name   - progress output to fifo (see manual)\n"
	  " -p name   - database path including final slash (default is ./db/)\n");
  fprintf(stderr, /* string length less than 509 bytes for ISO C89 */
	  " -f fmt    - output format (ISO8859_1 TeX HTML XML UTF8 ASCII)\n"
	  " -l num    - threshold grey level 0<160<=255 (0 = autodetect)\n"
	  " -d num    - dust_size (remove small clusters, -1 = autodetect)\n"
	  " -s num    - spacewidth/dots (0 = autodetect)\n"
	  " -v num    - verbose (see manual page)\n"
	  " -c string - list of chars (debugging, see manual)\n"
	  " -C string - char filter (ex. hexdigits: ""0-9A-Fx"", only ASCII)\n"
	  " -m num    - operation modes (bitpattern, see manual)\n");
  fprintf(stderr, /* string length less than 509 bytes for ISO C89 */
	  " -a num    - value of certainty (in percent, 0..100, default=95)\n"
	  " -u string - output this string for every unrecognized character\n");
  fprintf(stderr, /* string length less than 509 bytes for ISO C89 */
	  " examples:\n"
	  "\tgocr -m 4 text1.pbm                   # do layout analyzis\n"
	  "\tgocr -m 130 -p ./database/ text1.pbm  # extend database\n"
	  "\tdjpeg -pnm -gray text.jpg | gocr -    # use jpeg-file via pipe\n"
	  "\n");
  fprintf(stderr, " webpage: http://jocr.sourceforge.net/ (may out of date)\n");
  fprintf(stderr, " mirror:  http://www-e.uni-magdeburg.de/jschulen/ocr/\n");
  exit(0);
}

#ifdef HAVE_GETTIMEOFDAY
/* from the glibc documentation */
static int timeval_subtract (struct timeval *result, struct timeval *x, 
    struct timeval *y) {

  /* Perform the carry for the later subtraction by updating Y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     `tv_usec' is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}
#endif

static void process_arguments(job_t *job, int argn, char *argv[])
{
  int i;
  char *s1;

  assert(job);

  if (argn <= 1) {
    out_version(1);
    exit(0);
  }
#ifdef HAVE_PGM_H
  pnm_init(&argn, &argv);
#endif
  
  /* process arguments */
  for (i = 1; i < argn; i++) {
    if (strcmp(argv[i], "--help") == 0)
      help(); /* and quits */
    if (argv[i][0] == '-' && argv[i][1] != 0) {
      s1 = "";
      if (i + 1 < argn)
	s1 = argv[i + 1];
      switch (argv[i][1]) {
      case 'h': /* help */
	help();
	break;
      case 'i': /* input image file */
	job->src.fname = s1;
	i++;
	break;
      case 'e': /* logging file */
	if (s1[0] == '-' && s1[1] == '\0') {
#ifdef HAVE_UNISTD_H
          dup2(STDOUT_FILENO, STDERR_FILENO); /* -e /dev/stdout  works */
#else
	  fprintf(stderr, "stderr redirection not possible without unistd.h\n");
#endif           
	}
	else if (!freopen(s1, "w", stderr)) {
	  fprintf(stderr, "stderr redirection to %s failed\n", s1);
	}
	i++;
	break;
      case 'p': /* database path */
	job->cfg.db_path=s1;
	i++;
	break;
      case 'o': /* output file */
	if (s1[0] == '-' && s1[1] == '\0') {	/* default */
	}
	else if (!freopen(s1, "w", stdout)) {
	  fprintf(stderr, "stdout redirection to %s failed\n", s1);
	};
	i++;
	break;
      case 'f': /* output format */
        if (strcmp(s1, "ISO8859_1") == 0) job->cfg.out_format=ISO8859_1; else
        if (strcmp(s1, "TeX")       == 0) job->cfg.out_format=TeX; else
        if (strcmp(s1, "HTML")      == 0) job->cfg.out_format=HTML; else
        if (strcmp(s1, "XML")       == 0) job->cfg.out_format=XML; else
        if (strcmp(s1, "SGML")      == 0) job->cfg.out_format=SGML; else
        if (strcmp(s1, "UTF8")      == 0) job->cfg.out_format=UTF8; else
        if (strcmp(s1, "ASCII")     == 0) job->cfg.out_format=ASCII; else
        fprintf(stderr,"Warning: unknown format (-f %s)\n",s1);
        i++;
        break;
      case 'c': /* list of chars (_ = not recognized chars) */
	job->cfg.lc = s1;
	i++;
	break;
      case 'C': /* char filter, default: NULL (all chars) */
        /* ToDo: UTF8 input, wchar */
	job->cfg.cfilter = s1;
	i++;
	break;
      case 'd': /* dust size */
	job->cfg.dust_size = atoi(s1);
	i++;
	break;
      case 'l': /* grey level 0<160<=255, 0 for autodetect */
	job->cfg.cs = atoi(s1);
	i++;
	break;
      case 's': /* spacewidth/dots (0 = autodetect) */
	job->cfg.spc = atoi(s1);
	i++;
	break;
      case 'v': /* verbose mode */
	job->cfg.verbose |= atoi(s1);
	i++;
	break;
      case 'm': /* operation modes */
	job->cfg.mode |= atoi(s1);
	i++;
	break;
      case 'n': /* numbers only */
	job->cfg.only_numbers = atoi(s1);
	i++;
	break;
      case 'x': /* initialize progress output s1=fname */
	ini_progress(s1);
	i++;
	break;
      case 'a': /* set certainty */
	job->cfg.certainty = atoi(s1);;
	i++;
	break;
      case 'u': /* output marker for unrecognized chars */
        job->cfg.unrec_marker = s1;
        i++;
        break;
      default:
	fprintf(stderr, "# unknown option use -h for help\n");
      }
      continue;
    }
    else /* argument can be filename v0.2.5 */ if (argv[i][0] != '-'
						   || argv[i][1] == '\0' ) {
      job->src.fname = argv[i];
    }
  }
}

static void mark_start(job_t *job) {
  assert(job);

  if (job->cfg.verbose) {
    out_version(0);
    /* insert some helpful info for support */
    fprintf(stderr, "# compiled: " __DATE__ );
#if defined(__GNUC__)
    fprintf(stderr, " GNUC-%d", __GNUC__ );
#endif
#ifdef __GNUC_MINOR__
    fprintf(stderr, ".%d", __GNUC_MINOR__ );
#endif
#if defined(__linux)
    fprintf(stderr, " linux");
#elif defined(__unix)
    fprintf(stderr, " unix");
#endif
#if defined(__WIN32) || defined(__WIN32__)
    fprintf(stderr, " WIN32");
#endif
#if defined(__WIN64) || defined(__WIN64__)
    fprintf(stderr, " WIN64");
#endif
#if defined(__VERSION__)
    fprintf(stderr, " version " __VERSION__ );
#endif
    fprintf(stderr, "\n");
    fprintf(stderr,
      "# options are: -l %d -s %d -v %d -c %s -m %d -d %d -n %d -a %d -C \"%s\"\n",
      job->cfg.cs, job->cfg.spc, job->cfg.verbose, job->cfg.lc, job->cfg.mode, 
      job->cfg.dust_size, job->cfg.only_numbers, job->cfg.certainty,
      job->cfg.cfilter);
    fprintf(stderr, "# file: %s\n", job->src.fname);
#ifdef USE_UNICODE
    fprintf(stderr,"# using unicode\n");
#endif
#ifdef HAVE_GETTIMEOFDAY
    gettimeofday(&job->tmp.init_time, NULL);
#endif
  }
}



static int read_picture(job_t *job) {
  int rc=0;
  assert(job);

  if (strstr(job->src.fname, ".pcx"))
    readpcx(job->src.fname, &job->src.p, job->cfg.verbose);
  else
    rc=readpgm(job->src.fname, &job->src.p, job->cfg.verbose);
  return rc; /* 1 for multiple images, -1 on error, 0 else */
}



// function to creae spu_threads and call them
void *ppu_pthread_function(void *arg) {
	
	
    job_init_image(job); /* initialing image */

    mark_start(job); // starting work on image

  spe_context_ptr_t ctx;
  unsigned int entry = SPE_DEFAULT_ENTRY;
  
  ctx = *((spe_context_ptr_t *)arg);
  if (spe_context_run(ctx, &entry, 0, NULL, NULL, NULL) < 0) {
    perror ("Failed running context");
    exit (1);
  }
  pthread_exit(NULL);
}


//function to create several threads for spu
void create_threads(int n){
	for(i=0; i<n; i++) {
    /* Create context */
    if ((ctxs[i] = spe_context_create (0, NULL)) == NULL) {
      perror ("Failed creating context");
      exit (1);
    }
    /* Load program into context */
    if (spe_program_load (ctxs[i], &ocr_spu)) {
      perror ("Failed loading program");
      exit (1);
    }
    /* Create thread for each SPE context */
    if (pthread_create (&threads[i], NULL, &ppu_pthread_function, &ctxs[i]))  {
      perror ("Failed creating thread");
      exit (1);
    }
  }

  /* Wait for SPU-thread to complete execution.  */
  for (i=0; i<spu_threads; i++) {
    if (pthread_join (threads[i], NULL)) {
      perror("Failed pthread_join");
      exit (1);
    }

    /* Destroy context */
    if (spe_context_destroy (ctxs[i]) != 0) {
      perror("Failed destroying context");
      exit (1);
    }
  }

  printf("\nThe program has successfully executed.\n");

  return (0);
   
}


int main( int argc, char *argv)
{
  	
  // GOCR part being called on ppu for loading the image
  job_t job1, *job; /* fixme, dont want global variables for lib */
  job=OCR_JOB=&job1;

  setvbuf(stdout, (char *) NULL, _IONBF, 0);	/* not buffered */

  job_init(job); /* init cfg and db */

  process_arguments(job, argn, argv);
   /* load character data base (JS1002: now outside pgm2asc) */
  if ( job->cfg.mode & 2 ) /* check for db-option flag */
    load_db(job);
    /* load_db uses readpnm() and would conflict with multi images */
        

  int i, spu_threads;
  spe_context_ptr_t ctxs[MAX_SPU_THREADS];

  /* Determine the number of SPE threads to create.
   */
  spu_threads = spe_cpu_info_get(SPE_COUNT_USABLE_SPES, -1);
  if (spu_threads > MAX_SPU_THREADS) spu_threads = MAX_SPU_THREADS;

  /* Create several SPE-threads to execute 'simple_spu'.
   */
   create_threads(spu_threads);
   pthread_exit(NULL);
   return 0;
  
}
