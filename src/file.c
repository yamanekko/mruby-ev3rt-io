/*
** file.c - File class
*/

#include "mruby.h"
#include "mruby/class.h"
#include "mruby/data.h"
#include "mruby/string.h"
#include "mruby/ext/io.h"

#include "mruby/error.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <limits.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define UNLINK f_unlink
#define GETCWD f_getcwd
#define CHMOD(a, b) 0
#define MAXPATHLEN 1024
#include <sys/file.h>
#include <libgen.h>
#include <sys/param.h>
#include <pwd.h>

#include "fatfs_dri.h"

#define FILE_SEPARATOR "/"

#ifndef LOCK_SH
#define LOCK_SH 1
#endif
#ifndef LOCK_EX
#define LOCK_EX 2
#endif
#ifndef LOCK_NB
#define LOCK_NB 4
#endif
#ifndef LOCK_UN
#define LOCK_UN 8
#endif

#define STAT(p, s)        stat(p, s)



mrb_value
mrb_file_s_umask(mrb_state *mrb, mrb_value klass)
{
  /* nothing to do on FAT */
  return mrb_fixnum_value(0);
}

static mrb_value
mrb_file_s_unlink(mrb_state *mrb, mrb_value obj)
{
  mrb_value *argv;
  mrb_value pathv;
  mrb_int argc, i;
  const char *path;

  mrb_get_args(mrb, "*", &argv, &argc);
  for (i = 0; i < argc; i++) {
    pathv = mrb_convert_type(mrb, argv[i], MRB_TT_STRING, "String", "to_str");
    path = mrb_string_value_cstr(mrb, &pathv);
    if (UNLINK(path) < 0) {
      mrb_sys_fail(mrb, path);
    }
  }
  return mrb_fixnum_value(argc);
}

static mrb_value
mrb_file_s_rename(mrb_state *mrb, mrb_value obj)
{
  mrb_value from, to;
  const char *src, *dst;

  mrb_get_args(mrb, "SS", &from, &to);
  src = mrb_string_value_cstr(mrb, &from);
  dst = mrb_string_value_cstr(mrb, &to);
  if (f_rename(src, dst) < 0) {
    if (CHMOD(dst, 0666) == 0 && UNLINK(dst) == 0 && f_rename(src, dst) == 0) {
      return mrb_fixnum_value(0);
    }
    mrb_sys_fail(mrb, mrb_str_to_cstr(mrb, mrb_format(mrb, "(%S, %S)", from, to)));
  }
  return mrb_fixnum_value(0);
}

static inline char *
file_basename(char *file)
{
  char *p = strrchr(file, '/');
  if (p) {
    return p + 1;
  } else {
    return file;
  }
}

static mrb_value
mrb_file_basename(mrb_state *mrb, mrb_value klass)
{
  char *bname, *path;
  mrb_value s;
  mrb_get_args(mrb, "S", &s);
  path = mrb_str_to_cstr(mrb, s);
  if ((bname = file_basename(path)) == NULL) {
    mrb_sys_fail(mrb, "basename");
  }
  return mrb_str_new_cstr(mrb, bname);
}

mrb_value
mrb_file__getwd(mrb_state *mrb, mrb_value klass)
{
  /* XXX f_getwd() is not supported on EV3RT */
  return mrb_nil_value();

  /*
  mrb_value path;

  path = mrb_str_buf_new(mrb, MAXPATHLEN);
  if (GETCWD(RSTRING_PTR(path), MAXPATHLEN) != FR_OK) {
    mrb_sys_fail(mrb, "getcwd(2)");
  }
  mrb_str_resize(mrb, path, strlen(RSTRING_PTR(path)));
  return path;
  */
}

static mrb_value
mrb_file__gethome(mrb_state *mrb, mrb_value klass)
{
  return mrb_nil_value();
}

void
mrb_init_file(mrb_state *mrb)
{
  struct RClass *io, *file, *cnst;

  io   = mrb_class_get(mrb, "IO");
  file = mrb_define_class(mrb, "File", io);
  MRB_SET_INSTANCE_TT(file, MRB_TT_DATA);
  mrb_define_class_method(mrb, file, "delete", mrb_file_s_unlink, MRB_ARGS_ANY());
  mrb_define_class_method(mrb, file, "unlink", mrb_file_s_unlink, MRB_ARGS_ANY());
  mrb_define_class_method(mrb, file, "rename", mrb_file_s_rename, MRB_ARGS_REQ(2));

  mrb_define_class_method(mrb, file, "basename",  mrb_file_basename,   MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, file, "_getwd",    mrb_file__getwd,     MRB_ARGS_NONE());
  mrb_define_class_method(mrb, file, "_gethome",  mrb_file__gethome,   MRB_ARGS_OPT(1));

  cnst = mrb_define_module_under(mrb, file, "Constants");
  mrb_define_const(mrb, cnst, "LOCK_SH", mrb_fixnum_value(LOCK_SH));
  mrb_define_const(mrb, cnst, "LOCK_EX", mrb_fixnum_value(LOCK_EX));
  mrb_define_const(mrb, cnst, "LOCK_UN", mrb_fixnum_value(LOCK_UN));
  mrb_define_const(mrb, cnst, "LOCK_NB", mrb_fixnum_value(LOCK_NB));
  mrb_define_const(mrb, cnst, "SEPARATOR", mrb_str_new_cstr(mrb, FILE_SEPARATOR));
}
