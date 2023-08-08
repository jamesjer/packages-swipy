/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        jan@swi-prolog.org
    WWW:           http://www.swi-prolog.org
    Copyright (c)  2023, SWI-Prolog Solutions b.v.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in
       the documentation and/or other materials provided with the
       distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

static PyObject *PyExcProlog_store = NULL;
static PyObject *
PyExcProlog(void)
{ if ( !PyExcProlog_store )
    PyExcProlog_store = PyErr_NewException("janus.PrologError", NULL, NULL);
  return PyExcProlog_store;
}

static PyObject *
mod_janus(void)
{ static PyObject *janus = NULL;

  if ( !janus )
  { PyObject *janus_name = NULL;

    if ( (janus_name=PyUnicode_FromString("janus")) )
      janus = PyImport_Import(janus_name);

    Py_CLEAR(janus_name);
  }

  return janus;
}


static void
Py_SetPrologErrorFromObject(PyObject *obj)
{ PyObject *janus;
  PyObject *constructor = NULL;
  PyObject *argv = NULL;

  if ( (janus=mod_janus()) &&
       (constructor=PyObject_GetAttrString(janus, "PrologError")) &&
       (argv=PyTuple_New(1)) )
  { Py_INCREF(obj);
    PyTuple_SetItem(argv, 0, obj);
    PyObject *ex = PyObject_CallObject(constructor, argv);
    if ( ex )
      PyErr_SetObject(PyExcProlog(), ex);
  }

  Py_CLEAR(constructor);
  Py_CLEAR(argv);
}


static void
Py_SetPrologError(term_t ex)
{ PyObject *obj = py_record(ex);
  Py_SetPrologErrorFromObject(obj);
  Py_CLEAR(obj);
}

static void
Py_SetPrologErrorFromChars(const char *s)
{ PyObject *msg = PyUnicode_FromString(s);
  Py_SetPrologErrorFromObject(msg);
  Py_CLEAR(msg);
}


static int
unify_input(term_t t, Py_ssize_t arity, PyObject *args)
{ if ( arity == 1 )		/* no input arguments */
    return PL_put_dict(t, ATOM_pydict, 0, NULL, 0);
  else
    return py_unify(t, PyTuple_GetItem(args, 1), 0);
}

static int
keep_bindings(PyObject *args)
{ PyObject *kp;

  return ( PyTuple_GET_SIZE(args) >= 3 &&
	   (kp=PyTuple_GetItem(args, 2))&&
	   PyBool_Check(kp) &&
	   PyLong_AsLong(kp) );
}

static predicate_t pred = NULL;
static module_t user = 0;

static PyObject *
swipl_call(PyObject *self, PyObject *args)
{ PyObject *out = NULL;
  fid_t fid;
  Py_ssize_t arity = PyTuple_GET_SIZE(args);

  if ( arity == 0 || arity > 3 )
  { PyErr_SetString(PyExc_TypeError,
		    "swipl.call(query,bindings,keep) takes 1..3 arguments");
    return NULL;
  }

  if ( !pred || !user )
  { pred = PL_predicate("py_call_string", 3, "janus");
    user = PL_new_module(PL_new_atom("user"));
  }

  if ( (fid=PL_open_foreign_frame()) )
  { term_t av = PL_new_term_refs(3);

    if ( py_unify(av+0, PyTuple_GetItem(args, 0), 0) &&
	 unify_input(av+1, arity, args) )
    { qid_t qid = PL_open_query(user, PL_Q_CATCH_EXCEPTION|PL_Q_EXT_STATUS,
				pred, av);
      int rc = PL_next_solution(qid);
      switch(rc)
      { case PL_S_TRUE:
	case PL_S_LAST:
	  if ( !py_from_prolog(av+2, &out) )
	  { term_t ex = PL_exception(0);

	    assert(ex);
	    ex = PL_copy_term_ref(ex);
	    PL_clear_exception();
	    Py_SetPrologError(ex);
	  }
	  break;
	case PL_S_FALSE:
	  out = PyBool_FromLong(0);
	  break;
	case PL_S_EXCEPTION:
	  Py_SetPrologError(PL_exception(qid));
	  break;
      }
      PL_cut_query(qid);
    }

    if ( keep_bindings(args) )
      PL_close_foreign_frame(fid);
    else
      PL_discard_foreign_frame(fid);
  }

  return out;
}

static void
tuple_set_int(int i, PyObject *tuple, int64_t val)
{ PyObject *v = PyLong_FromLongLong(val);
  Py_INCREF(v);
  PyList_SetItem(tuple, i, v);
}

#define STATE_LIST_LENGTH 4	/* fid,qid,av,keep */

static PyObject *
swipl_open_query(PyObject *self, PyObject *args)
{ PyObject *out = NULL;
  fid_t fid;
  Py_ssize_t arity = PyTuple_GET_SIZE(args);

  if ( arity == 0 || arity > 3 )
  { PyErr_SetString(PyExc_TypeError, "swipl.call(query,bindings,keep) takes 1..3 arguments");
    return NULL;
  }

  if ( !pred || !user )
  { pred = PL_predicate("py_call_string", 3, "janus");
    user = PL_new_module(PL_new_atom("user"));
  }

  if ( (fid=PL_open_foreign_frame()) )
  { term_t av = PL_new_term_refs(3);

    if ( py_unify(av+0, PyTuple_GetItem(args, 0), 0) &&
	 unify_input(av+1, arity, args) )
    { qid_t qid = PL_open_query(user, PL_Q_CATCH_EXCEPTION|PL_Q_EXT_STATUS, pred, av);

      out = PyList_New(STATE_LIST_LENGTH);
      tuple_set_int(0, out, fid);
      tuple_set_int(1, out, (int64_t)qid);
      tuple_set_int(2, out, av);
      tuple_set_int(3, out, keep_bindings(args));

      return out;
    }
  }

  Py_SetPrologError(PL_exception(0));
  return NULL;
}


static int
Py_GetInt64Arg(int i, PyObject *tp, int64_t *vp)
{ PyObject *arg = PyList_GetItem(tp, i);
  if ( !PyLong_Check(arg) )
  { PyErr_SetString(PyExc_TypeError, "query type arg must be integer");
    return FALSE;
  }
  *vp = PyLong_AsLongLong(arg);
  return TRUE;
}

static int
query_parms(PyObject *args, PyObject **tpp, fid_t *fid, qid_t *qid, term_t *av, int *keep)
{ if ( PyTuple_GET_SIZE(args) != 1 )
  { PyErr_SetString(PyExc_TypeError, "Method expects a list [fid,qid,av,keep]");
    return FALSE;
  }

  PyObject *tp = PyTuple_GetItem(args, 0);
  if ( !PyList_Check(tp) || PyList_GET_SIZE(tp) != STATE_LIST_LENGTH )
  { PyErr_SetString(PyExc_TypeError, "Method expects a list [fid,qid,av,keep]");
    return FALSE;
  }

  int64_t tav[STATE_LIST_LENGTH];
  *tpp = tp;
  if ( !Py_GetInt64Arg(0, tp, &tav[0]) ||
       !Py_GetInt64Arg(1, tp, &tav[1]) ||
       !Py_GetInt64Arg(2, tp, &tav[2]) ||
       !Py_GetInt64Arg(3, tp, &tav[3]) )
    return FALSE;
  *fid  = tav[0];
  *qid  = (qid_t)tav[1];
  *av   = tav[2];
  *keep = (int)tav[3];

  return TRUE;
}

static PyObject *
swipl_next_solution(PyObject *self, PyObject *args)
{ fid_t fid;
  qid_t qid;
  term_t av;
  int done = FALSE;
  PyObject *tp;
  int keep;

  if ( !query_parms(args, &tp, &fid, &qid, &av, &keep) )
    return NULL;
  if ( !qid )
    return PyBool_FromLong(0);

  int rc = PL_next_solution(qid);
  PyObject *out = NULL;

  switch(rc)
  { case PL_S_LAST:
      PL_cut_query(qid);
      done = TRUE;
      /*FALLTHROUGH*/
    case PL_S_TRUE:
      py_from_prolog(av+2, &out);
      break;
    case PL_S_FALSE:
      out = PyBool_FromLong(0);
      PL_cut_query(qid);
      done = TRUE;
      break;
    case PL_S_EXCEPTION:
      Py_SetPrologError(PL_exception(qid));
      PL_cut_query(qid);
      done = TRUE;
    case PL_S_NOT_INNER:
      Py_SetPrologErrorFromChars("swipl.next_solution(): not inner query");
      return NULL;
      break;
  }
  if ( done )
  { if ( keep )
      PL_close_foreign_frame(fid);
    else
      PL_discard_foreign_frame(fid);
    tuple_set_int(1, tp, 0);   /* set qid to 0 */
  }

  return out;
}

static PyObject *
swipl_close_query(PyObject *self, PyObject *args)
{ fid_t fid;
  qid_t qid;
  term_t av;
  PyObject *tp;
  int keep;

  if ( !query_parms(args, &tp, &fid, &qid, &av, &keep) )
    return NULL;

  if ( qid )
  { if ( PL_cut_query(qid) == PL_S_NOT_INNER )
    { Py_SetPrologErrorFromChars("swipl.next_solution(): not inner query");
      return NULL;
    }
    if ( keep )
      PL_close_foreign_frame(fid);
    else
      PL_discard_foreign_frame(fid);
    tuple_set_int(1, tp, 0);   /* set qid to 0 */
  }

  Py_RETURN_NONE;
}


static PyObject *
swipl_erase(PyObject *self, PyObject *args)
{ PyObject *rec = NULL;
  PyObject *rc = NULL;

  if ( PyTuple_GET_SIZE(args) != 1 )
    goto error;
  rec = PyTuple_GetItem(args, 0);
  if ( py_is_record(rec) )
    rc = py_free_record(rec);
  Py_CLEAR(rec);
  if ( rc )
    return rc;

error:
  PyErr_SetString(PyExc_TypeError, "swipl.erase(Term) takes a Term");
  return NULL;
}



#ifdef PYTHON_PACKAGE

install_t install_janus(void);

static PyObject *
swipl_initialize(PyObject *self, PyObject *args)
{ Py_ssize_t argc = PyTuple_GET_SIZE(args);
  const char* *argv = malloc((argc+1)*sizeof(*argv));

  memset(argv, 0, (argc+1)*sizeof(*argv));
  for(Py_ssize_t i=0; i<argc; i++)
  { PyObject *a = PyTuple_GetItem(args, i);
    if ( PyUnicode_Check(a) )
    { argv[i] = PyUnicode_AsUTF8AndSize(a, NULL);
    } else
    { assert(0);
    }
  }

  if ( !PL_initialise((int)argc, (char**)argv) )
  { Py_SetPrologErrorFromChars("Failed to initialize SWI-Prolog");
    return NULL;
  }

  install_janus();

  fid_t fid;
  int rc = FALSE;
  predicate_t pred = PL_predicate("use_module", 1, "user");

  if ( (fid=PL_open_foreign_frame()) )
  { term_t av;

    rc = ( (av=PL_new_term_refs(1)) &&
	    PL_unify_term(av+0,
			  PL_FUNCTOR_CHARS, "library", 1,
			    PL_CHARS, "janus") &&
	   PL_call_predicate(NULL, PL_Q_NORMAL, pred, av) );
    PL_discard_foreign_frame(fid);
  }

  if ( !rc )
  { Py_SetPrologErrorFromChars("Failed to load library(janus) into Prolog");
    return NULL;
  }

  Py_RETURN_TRUE;
}

#endif /*PYTHON_PACKAGE*/

static PyMethodDef swiplMethods[] =
{ {"call", swipl_call, METH_VARARGS,
   "Execute a Prolog query."},
  {"open_query", swipl_open_query, METH_VARARGS,
   "Open a Prolog query."},
  {"next_solution", swipl_next_solution, METH_VARARGS,
   "Compute the next answer."},
  {"close_query", swipl_close_query, METH_VARARGS,
   "Close an open query."},
  {"erase", swipl_erase, METH_VARARGS,
   "Erase a record."},
#ifdef PYTHON_PACKAGE
  {"initialize", swipl_initialize, METH_VARARGS,
   "Initialize SWI-Prolog."},
#endif
  {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef swipl_module =
{ PyModuleDef_HEAD_INIT,
  "swipl",   /* name of module */
  NULL,      /* module documentation, may be NULL */
  -1,        /* size of per-interpreter state of the module,
		or -1 if the module keeps state in global variables. */
  swiplMethods
};

PyMODINIT_FUNC
PyInit_swipl(void)
{ return PyModule_Create(&swipl_module);
}