/*
** io.c - IO class
*/

#include "mruby.h"
#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/data.h"
#include "mruby/hash.h"
#include "mruby/string.h"
#include "mruby/variable.h"
#include "mruby/ext/io.h"

#include "mruby/error.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <sys/wait.h>
#include <unistd.h>

#include <fcntl.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

struct timeval {
  time_t tv_sec;
  suseconds_t tv_usec;
};

static int mrb_io_modestr_to_flags(mrb_state *mrb, const char *modestr);
static int mrb_io_flags_to_modenum(mrb_state *mrb, int flags);
static void fptr_finalize(mrb_state *mrb, struct mrb_io *fptr, int noraise);

static int
mrb_io_modestr_to_flags(mrb_state *mrb, const char *mode)
{
  int flags = 0;
  const char *m = mode;

  switch (*m++) {
    case 'r':
      flags |= FMODE_READABLE;
      break;
    case 'w':
      flags |= FMODE_WRITABLE | FMODE_CREATE | FMODE_TRUNC;
      break;
    case 'a':
      flags |= FMODE_WRITABLE | FMODE_APPEND | FMODE_CREATE;
      break;
    default:
      mrb_raisef(mrb, E_ARGUMENT_ERROR, "illegal access mode %S", mrb_str_new_cstr(mrb, mode));
  }

  while (*m) {
    switch (*m++) {
      case 'b':
        flags |= FMODE_BINMODE;
        break;
      case '+':
        flags |= FMODE_READWRITE;
        break;
      case ':':
        /* XXX: PASSTHROUGH*/
      default:
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "illegal access mode %S", mrb_str_new_cstr(mrb, mode));
    }
  }

  return flags;
}

static int
mrb_io_flags_to_modenum(mrb_state *mrb, int flags)
{
  int modenum = 0;

  switch(flags & (FMODE_READABLE|FMODE_WRITABLE|FMODE_READWRITE)) {
    case FMODE_READABLE:
      modenum = O_RDONLY;
      break;
    case FMODE_WRITABLE:
      modenum = O_WRONLY;
      break;
    case FMODE_READWRITE:
      modenum = O_RDWR;
      break;
  }

  if (flags & FMODE_APPEND) {
    modenum |= O_APPEND;
  }
  if (flags & FMODE_TRUNC) {
    modenum |= O_TRUNC;
  }
  if (flags & FMODE_CREATE) {
    modenum |= O_CREAT;
  }
  /* ignore O_BINARY */
  return modenum;
}

static void
mrb_io_free(mrb_state *mrb, void *ptr)
{
  struct mrb_io *io = (struct mrb_io *)ptr;
  if (io != NULL) {
    fptr_finalize(mrb, io, TRUE);
    mrb_free(mrb, io);
  }
}

struct mrb_data_type mrb_io_type = { "IO", mrb_io_free };

static struct mrb_io *
mrb_io_alloc(mrb_state *mrb)
{
  struct mrb_io *fptr;

  fptr = (struct mrb_io *)mrb_malloc(mrb, sizeof(struct mrb_io));
  fptr->fd = -1;
  fptr->fd2 = -1;
  fptr->pid = 0;
  fptr->writable = 0;
  fptr->sync = 0;
  return fptr;
}

#ifndef NOFILE
#define NOFILE 64
#endif

mrb_value
mrb_io_initialize(mrb_state *mrb, mrb_value io)
{
  struct mrb_io *fptr;
  mrb_int fd;
  mrb_value mode, opt;
  int flags;

  mode = opt = mrb_nil_value();

  mrb_get_args(mrb, "i|So", &fd, &mode, &opt);
  if (mrb_nil_p(mode)) {
    mode = mrb_str_new_cstr(mrb, "r");
  }
  if (mrb_nil_p(opt)) {
    opt = mrb_hash_new(mrb);
  }

  flags = mrb_io_modestr_to_flags(mrb, mrb_string_value_cstr(mrb, &mode));

  mrb_iv_set(mrb, io, mrb_intern_cstr(mrb, "@buf"), mrb_str_new_cstr(mrb, ""));
  mrb_iv_set(mrb, io, mrb_intern_cstr(mrb, "@pos"), mrb_fixnum_value(0));

  fptr = DATA_PTR(io);
  if (fptr != NULL) {
    fptr_finalize(mrb, fptr, 0);
    mrb_free(mrb, fptr);
  }
  fptr = mrb_io_alloc(mrb);

  DATA_TYPE(io) = &mrb_io_type;
  DATA_PTR(io) = fptr;

  fptr->fd = fd;
  fptr->writable = ((flags & FMODE_WRITABLE) != 0);
  fptr->sync = 0;
  return io;
}

static void
fptr_finalize(mrb_state *mrb, struct mrb_io *fptr, int noraise)
{
  int n = 0;

  if (fptr == NULL) {
    return;
  }

  if (fptr->fd > 2) {
    n = close(fptr->fd);
    if (n == 0) {
      fptr->fd = -1;
    }
  }
  if (fptr->fd2 > 2) {
    n = close(fptr->fd2);
    if (n == 0) {
      fptr->fd2 = -1;
    }
  }

  if (!noraise && n != 0) {
    mrb_sys_fail(mrb, "fptr_finalize failed.");
  }
}

mrb_value
mrb_io_s_for_fd(mrb_state *mrb, mrb_value klass)
{
  struct RClass *c = mrb_class_ptr(klass);
  enum mrb_vtype ttype = MRB_INSTANCE_TT(c);
  mrb_value obj;

  /* copied from mrb_instance_alloc() */
  if (ttype == 0) ttype = MRB_TT_OBJECT;
  obj = mrb_obj_value((struct RObject*)mrb_obj_alloc(mrb, ttype, c));
  return mrb_io_initialize(mrb, obj);
}

mrb_value
mrb_io_s_sysclose(mrb_state *mrb, mrb_value klass)
{
  mrb_int fd;
  mrb_get_args(mrb, "i", &fd);
  if (close(fd) == -1) {
    mrb_sys_fail(mrb, "close");
  }
  return mrb_fixnum_value(0);
}

mrb_value
mrb_io_s_sysopen(mrb_state *mrb, mrb_value klass)
{
  mrb_value path = mrb_nil_value();
  mrb_value mode = mrb_nil_value();
  mrb_int fd, flags, perm = -1;
  const char *pat;
  int modenum, retry = FALSE;

  mrb_get_args(mrb, "S|Si", &path, &mode, &perm);
  if (mrb_nil_p(mode)) {
    mode = mrb_str_new_cstr(mrb, "r");
  }
  if (perm < 0) {
    perm = 0666;
  }

  pat = mrb_string_value_cstr(mrb, &path);
  flags = mrb_io_modestr_to_flags(mrb, mrb_string_value_cstr(mrb, &mode));
  modenum = mrb_io_flags_to_modenum(mrb, flags);

 reopen:
  fd = open(pat, modenum, perm);
  if (fd == -1) {
    if (!retry) {
      switch (errno) {
      case ENFILE:
      case EMFILE:
        mrb_garbage_collect(mrb);
        retry = TRUE;
        goto reopen;
      }
    }
    mrb_sys_fail(mrb, pat);
  }

  return mrb_fixnum_value(fd);
}

mrb_value
mrb_io_sysread(mrb_state *mrb, mrb_value io)
{
  struct mrb_io *fptr;
  mrb_value buf = mrb_nil_value();
  mrb_int maxlen;
  int ret;

  mrb_get_args(mrb, "i|S", &maxlen, &buf);
  if (maxlen < 0) {
    return mrb_nil_value();
  }

  if (mrb_nil_p(buf)) {
    buf = mrb_str_new(mrb, NULL, maxlen);
  }
  if (RSTRING_LEN(buf) != maxlen) {
    buf = mrb_str_resize(mrb, buf, maxlen);
  }

  fptr = (struct mrb_io *)mrb_get_datatype(mrb, io, &mrb_io_type);
  ret = read(fptr->fd, RSTRING_PTR(buf), maxlen);
  switch (ret) {
    case 0: /* EOF */
      if (maxlen == 0) {
        buf = mrb_str_new_cstr(mrb, "");
      } else {
        mrb_raise(mrb, E_EOF_ERROR, "sysread failed: End of File");
      }
      break;
    case -1: /* Error */
      mrb_sys_fail(mrb, "sysread failed");
      break;
    default:
      if (RSTRING_LEN(buf) != ret) {
        buf = mrb_str_resize(mrb, buf, ret);
      }
      break;
  }

  return buf;
}

mrb_value
mrb_io_sysseek(mrb_state *mrb, mrb_value io)
{
  struct mrb_io *fptr;
  int pos;
  mrb_int offset, whence = -1;

  mrb_get_args(mrb, "i|i", &offset, &whence);
  if (whence < 0) {
    whence = 0;
  }

  fptr = (struct mrb_io *)mrb_get_datatype(mrb, io, &mrb_io_type);
  pos = lseek(fptr->fd, offset, whence);
  if (pos < 0) {
    mrb_raise(mrb, E_IO_ERROR, "sysseek faield");
  }

  return mrb_fixnum_value(pos);
}

mrb_value
mrb_io_syswrite(mrb_state *mrb, mrb_value io)
{
  struct mrb_io *fptr;
  mrb_value str, buf;
  int fd, length;

  fptr = (struct mrb_io *)mrb_get_datatype(mrb, io, &mrb_io_type);
  if (! fptr->writable) {
    mrb_raise(mrb, E_IO_ERROR, "not opened for writing");
  }

  mrb_get_args(mrb, "S", &str);
  if (mrb_type(str) != MRB_TT_STRING) {
    buf = mrb_funcall(mrb, str, "to_s", 0);
  } else {
    buf = str;
  }

  if (fptr->fd2 == -1) {
    fd = fptr->fd;
  } else {
    fd = fptr->fd2;
  }
  length = write(fd, RSTRING_PTR(buf), RSTRING_LEN(buf));

  return mrb_fixnum_value(length);
}

mrb_value
mrb_io_close(mrb_state *mrb, mrb_value io)
{
  struct mrb_io *fptr;
  fptr = (struct mrb_io *)mrb_get_datatype(mrb, io, &mrb_io_type);
  if (fptr && fptr->fd < 0) {
    mrb_raise(mrb, E_IO_ERROR, "closed stream.");
  }
  fptr_finalize(mrb, fptr, FALSE);
  return mrb_nil_value();
}

mrb_value
mrb_io_closed(mrb_state *mrb, mrb_value io)
{
  struct mrb_io *fptr;
  fptr = (struct mrb_io *)mrb_get_datatype(mrb, io, &mrb_io_type);
  if (fptr->fd >= 0) {
    return mrb_false_value();
  }

  return mrb_true_value();
}

mrb_value
mrb_io_pid(mrb_state *mrb, mrb_value io)
{
  struct mrb_io *fptr;
  fptr = (struct mrb_io *)mrb_get_datatype(mrb, io, &mrb_io_type);

  if (fptr->pid > 0) {
    return mrb_fixnum_value(fptr->pid);
  }

  return mrb_nil_value();
}

mrb_value
mrb_io_fileno(mrb_state *mrb, mrb_value io)
{
  struct mrb_io *fptr;
  fptr = (struct mrb_io *)mrb_get_datatype(mrb, io, &mrb_io_type);
  return mrb_fixnum_value(fptr->fd);
}

mrb_value
mrb_io_close_on_exec_p(mrb_state *mrb, mrb_value io)
{
  mrb_raise(mrb, E_NOTIMP_ERROR, "IO#close_on_exec? is not supported on the platform");
  return mrb_false_value();
}

mrb_value
mrb_io_set_close_on_exec(mrb_state *mrb, mrb_value io)
{
  mrb_raise(mrb, E_NOTIMP_ERROR, "IO#close_on_exec= is not supported on the platform");
  return mrb_nil_value();
}

mrb_value
mrb_io_set_sync(mrb_state *mrb, mrb_value self)
{
  struct mrb_io *fptr;
  mrb_bool b;

  fptr = (struct mrb_io *)mrb_get_datatype(mrb, self, &mrb_io_type);
  if (fptr->fd < 0) {
    mrb_raise(mrb, E_IO_ERROR, "closed stream.");
  }

  mrb_get_args(mrb, "b", &b);
  fptr->sync = b;
  return mrb_bool_value(b);
}

mrb_value
mrb_io_sync(mrb_state *mrb, mrb_value self)
{
  struct mrb_io *fptr;

  fptr = (struct mrb_io *)mrb_get_datatype(mrb, self, &mrb_io_type);
  if (fptr->fd < 0) {
    mrb_raise(mrb, E_IO_ERROR, "closed stream.");
  }
  return mrb_bool_value(fptr->sync);
}

void
mrb_init_io(mrb_state *mrb)
{
  struct RClass *io;

  io      = mrb_define_class(mrb, "IO", mrb->object_class);
  MRB_SET_INSTANCE_TT(io, MRB_TT_DATA);

  mrb_include_module(mrb, io, mrb_module_get(mrb, "Enumerable")); /* 15.2.20.3 */
  mrb_define_class_method(mrb, io, "for_fd",  mrb_io_s_for_fd,   MRB_ARGS_ANY());
  mrb_define_class_method(mrb, io, "sysopen", mrb_io_s_sysopen, MRB_ARGS_ANY());

  mrb_define_method(mrb, io, "initialize", mrb_io_initialize, MRB_ARGS_ANY());    /* 15.2.20.5.21 (x)*/
  mrb_define_method(mrb, io, "sync",       mrb_io_sync,       MRB_ARGS_NONE());
  mrb_define_method(mrb, io, "sync=",      mrb_io_set_sync,   MRB_ARGS_REQ(1));
  mrb_define_method(mrb, io, "sysread",    mrb_io_sysread,    MRB_ARGS_ANY());
  mrb_define_method(mrb, io, "sysseek",    mrb_io_sysseek,    MRB_ARGS_REQ(1));
  mrb_define_method(mrb, io, "syswrite",   mrb_io_syswrite,   MRB_ARGS_REQ(1));
  mrb_define_method(mrb, io, "close",      mrb_io_close,      MRB_ARGS_NONE());   /* 15.2.20.5.1 */
  mrb_define_method(mrb, io, "close_on_exec=", mrb_io_set_close_on_exec, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, io, "close_on_exec?", mrb_io_close_on_exec_p,   MRB_ARGS_NONE());
  mrb_define_method(mrb, io, "closed?",    mrb_io_closed,     MRB_ARGS_NONE());   /* 15.2.20.5.2 */
  mrb_define_method(mrb, io, "pid",        mrb_io_pid,        MRB_ARGS_NONE());   /* 15.2.20.5.2 */
  mrb_define_method(mrb, io, "fileno",     mrb_io_fileno,     MRB_ARGS_NONE());


  mrb_gv_set(mrb, mrb_intern_cstr(mrb, "$/"), mrb_str_new_cstr(mrb, "\n"));
}
