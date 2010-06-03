#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <util.h>
#include <stdio.h>
#include <ext_job.h>
#include <ext_joblist.h>
#include <forward_model.h>
#include <subst_list.h>
#include <lsf_request.h>
#include <vector.h>
#include <parser.h>
/**
   This file implements a 'forward-model' object. I
*/



struct forward_model_struct {
  vector_type               * jobs;         /* The actual jobs in this forward model. */
  const ext_joblist_type    * ext_joblist;  /* This is the list of external jobs which have been installed - which we can choose from. */
  lsf_request_type          * lsf_request;  /* The lsf_requests needed for this forward model == NULL if we are not using lsf. */
  char                      * start_tag;
  char                      * end_tag;
};

//#define DEFAULT_START_TAG    "<"
//#define DEFAULT_END_TAG      ">"
#define DEFAULT_JOB_MODULE   "jobs.py"
#define DEFAULT_JOBLIST_NAME "jobList"



static char * forward_model_alloc_tagged_string(const forward_model_type * forward_model , const char * s) {
  return util_alloc_sprintf("%s%s%s" , forward_model->start_tag , s , forward_model->end_tag);
}


/*****************************************************************/


forward_model_type * forward_model_alloc(const ext_joblist_type * ext_joblist, bool statoil_mode , bool use_lsf , const char * start_tag , const char * end_tag) {
  forward_model_type * forward_model = util_malloc( sizeof * forward_model , __func__);
  
  forward_model->jobs        = vector_alloc_new();
  forward_model->ext_joblist = ext_joblist;
  if (use_lsf)
    forward_model->lsf_request = lsf_request_alloc(statoil_mode);
  else
    forward_model->lsf_request = NULL;
  
  forward_model->start_tag = util_alloc_string_copy( start_tag );
  forward_model->end_tag = util_alloc_string_copy( end_tag );
  return forward_model;
}


/**
   Allocates and returns a stringlist with all the names in the
   current forward_model.
*/
stringlist_type * forward_model_alloc_joblist( const forward_model_type * forward_model ) {
  stringlist_type * names = stringlist_alloc_new( );
  int i;
  for (i=0; i < vector_get_size( forward_model->jobs ); i++) {
    const ext_job_type * job = vector_iget_const( forward_model->jobs , i);
    stringlist_append_ref( names , ext_job_get_name( job ));
  }
  
  return names;
}


/**
   This function adds the job named 'job_name' to the forward model. The return
   value is the newly created ext_job instance. This can be used to set private
   arguments for this job.
*/

ext_job_type * forward_model_add_job(forward_model_type * forward_model , const char * job_name) {
  ext_job_type * new_job = ext_joblist_get_job_copy(forward_model->ext_joblist , job_name);
  vector_append_owned_ref( forward_model->jobs , new_job , ext_job_free__);
  return new_job;
}



/**
   This function is used to set private argument values to jobs in the
   forward model (i.e. the argument values passed in with KEY=VALUE
   pairs in the defining ().  

   The use of 'index' to get the job is unfortunate , however one
   forward model can contain several instances of the same job, it is
   therefor not possible to use name based lookup.
*/

void forward_model_iset_job_arg( forward_model_type * forward_model , int job_index , const char * arg , const char * value) {
  ext_job_type * job = vector_iget( forward_model->jobs , job_index );
  {
    char * tagged_arg = forward_model_alloc_tagged_string(forward_model , arg);
    ext_job_set_private_arg(job , tagged_arg , value);
    free( tagged_arg );
  }
}


void forward_model_clear( forward_model_type * forward_model ) {
  vector_clear( forward_model->jobs );
}



void forward_model_free( forward_model_type * forward_model) {
  vector_free( forward_model->jobs );
  if (forward_model->lsf_request != NULL) 
    lsf_request_free(forward_model->lsf_request);
  free(forward_model);
}




static void forward_model_update_lsf_request(forward_model_type * forward_model) {
  if (forward_model->lsf_request != NULL) {
    int ijob;
    lsf_request_reset( forward_model->lsf_request );
    for (ijob = 0; ijob < vector_get_size(forward_model->jobs); ijob++) {
      bool last_job = false;
      const ext_job_type * job = vector_iget_const( forward_model->jobs , ijob);
      if (ijob == (vector_get_size(forward_model->jobs) - 1))
	last_job = true;
      lsf_request_update(forward_model->lsf_request , job , last_job);
    }
  }
}



/**
   This function takes an input string of the type:

   JOB1  JOB2  JOB3(arg1 = value1, arg2 = value2, arg3= value3)

   And creates a forward model of it. Observe the following rules:
   
    * If the function takes private arguments it is NOT allowed with space
      between the end of the function name and the opening parenthesis.

*/


void forward_model_parse_init(forward_model_type * forward_model , const char * input_string ) {
  //tokenizer_type * tokenizer_alloc(" " , "\'\"" , ",=()" , NUlL , NULL , NULL);
  //stringlist_type * tokens = tokenizer_buffer( tokenizer , input_string , true);
  //stringlist_free( tokens );
  //tokenizer_free( tokenizer );

  char * p1                          = (char *) input_string;
  while (true) {
    ext_job_type *  current_job;
    char         * job_name;
    int            job_index;          
    {
      int job_length  = strcspn(p1 , " (");  /* Scanning until we meet ' ' or '(' */
      job_name = util_alloc_substring_copy(p1 , job_length);
      p1 += job_length;
    }
    job_index = vector_get_size( forward_model->jobs );
    current_job = forward_model_add_job(forward_model , job_name);

    if (*p1 == '(') {  /* The function has arguments. */
      int arg_length = strcspn(p1 , ")");
      if (arg_length == strlen(p1))
	util_abort("%s: paranthesis not terminated for job:%s \n",__func__ , job_name);
      {
	char  * arg_string          = util_alloc_substring_copy((p1 + 1) , arg_length - 1);
        ext_job_set_private_args_from_string( current_job , arg_string );
	p1 += (1 + arg_length);
        free( arg_string );
      }
    } 
    
    {
      int space_length = strspn(p1 , " ");
      p1 += space_length;
      if (*p1 == '(') 
	/* Detected lonesome '(' */
	util_abort("%s: found space between job:%s and \'(\' - aborting \n",__func__ , job_name);
    }

    /* 
       Now p1 should point at the next character after the job, 
       or after the ')' if the job has arguments.
    */
    
    if (*p1 == '\0') { /* We have parsed the whole string. */
      free(job_name);
      break;   
    }
  }
  forward_model_update_lsf_request(forward_model);
}



/*****************************************************************/

/*
  The name of the pyton module - and the variable in the module,
  used when running the remote jobs.
*/

void forward_model_python_fprintf(const forward_model_type * forward_model , const char * path, const subst_list_type * global_args) {
  char * module_file = util_alloc_filename(path , DEFAULT_JOB_MODULE , NULL);
  FILE * stream      = util_fopen(module_file , "w");
  int i;

  fprintf(stream , "%s = [" , DEFAULT_JOBLIST_NAME);
  for (i=0; i < vector_get_size(forward_model->jobs); i++) {
    const ext_job_type * job = vector_iget_const(forward_model->jobs , i);
    ext_job_python_fprintf(job , stream , global_args);
    if (i < (vector_get_size( forward_model->jobs ) - 1))
      fprintf(stream,",\n");
  }
  fprintf(stream , "]\n");
  fclose(stream);
  free(module_file);
}

#undef DEFAULT_JOB_MODULE   
#undef DEFAULT_JOBLIST_NAME 




forward_model_type * forward_model_alloc_copy(const forward_model_type * forward_model , bool statoil_mode) {
  int ijob;
  bool use_lsf = false;
  forward_model_type * new;

  if (forward_model->lsf_request != NULL)
    use_lsf = true;
  new = forward_model_alloc(forward_model->ext_joblist , statoil_mode , use_lsf , forward_model->start_tag , forward_model->end_tag);
  
  for (ijob = 0; ijob < vector_get_size(forward_model->jobs); ijob++) {
    const ext_job_type * job = vector_iget_const( forward_model->jobs , ijob);
    vector_append_owned_ref( new->jobs , ext_job_alloc_copy( job ) , ext_job_free__);
  }

  forward_model_update_lsf_request(new);
  
  return new;
}

ext_job_type * forward_model_iget_job( forward_model_type * forward_model , int index) {
  return vector_iget( forward_model->jobs , index );
}



void forward_model_fprintf(const forward_model_type * forward_model , FILE * stream) {
  int ijob;
  for (ijob = 0; ijob < vector_get_size(forward_model->jobs); ijob++) {
    ext_job_fprintf( vector_iget(forward_model->jobs , ijob) , stream);
    fprintf(stream , "  ");
  }
  fprintf(stream , "\n");
}


const ext_joblist_type * forward_model_get_joblist(const forward_model_type * forward_model) {
  return forward_model->ext_joblist;
}


const char * forward_model_get_lsf_request(const forward_model_type * forward_model) {
  if (forward_model->lsf_request != NULL)
    return lsf_request_get(forward_model->lsf_request);
  else
    return NULL;
}
