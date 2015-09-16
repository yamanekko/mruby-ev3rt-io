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

#include <sys/file.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <libgen.h>
#include <pwd.h>
#include <unistd.h>

#include <fcntl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fatfs_dri.h"

extern struct mrb_data_type mrb_io_type;

static int
mrb_stat(mrb_state *mrb, mrb_value obj, FILINFO *fno)
{
  FRESULT result;
  mrb_value tmp;
  mrb_value str_klass;

  str_klass = mrb_obj_value(mrb_class_get(mrb, "String"));
  tmp = mrb_funcall(mrb, obj, "is_a?", 1, str_klass);
  if (!mrb_test(tmp)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "only supported String");
  }
  const char *path = mrb_str_to_cstr(mrb, obj);
  //result = f_stat(path, fno);
  result = FR_INT_ERR;
  if (result == FR_OK) {
    return 0;
  } else {
    return -1;
  }
}

/*
 * Document-method: directory?
 *
 * call-seq:
 *   File.directory?(file_name)   ->  true or false
 *
 * Returns <code>true</code> if the named file is a directory,
 * or a symlink that points at a directory, and <code>false</code>
 * otherwise.
 *
 *    File.directory?(".")
 */

mrb_value
mrb_filetest_s_directory_p(mrb_state *mrb, mrb_value klass)
{
  FILINFO fno;
  mrb_value obj;

  mrb_get_args(mrb, "o", &obj);

  if (mrb_stat(mrb, obj, &fno) < 0)
    return mrb_false_value();
  if (fno.fattrib & AM_DIR)
    return mrb_true_value();

  return mrb_false_value();
}


/*
 * call-seq:
 *    File.exist?(file_name)    ->  true or false
 *    File.exists?(file_name)   ->  true or false
 *
 * Return <code>true</code> if the named file exists.
 */

mrb_value
mrb_filetest_s_exist_p(mrb_state *mrb, mrb_value klass)
{
  FILINFO fno;
  mrb_value obj;

  mrb_get_args(mrb, "o", &obj);
  if (mrb_stat(mrb, obj, &fno) < 0)
    return mrb_false_value();

  return mrb_true_value();
}

/*
 * call-seq:
 *    File.file?(file_name)   -> true or false
 *
 * Returns <code>true</code> if the named file exists and is a
 * regular file.
 */

mrb_value
mrb_filetest_s_file_p(mrb_state *mrb, mrb_value klass)
{
  FILINFO fno;
  mrb_value obj;

  mrb_get_args(mrb, "o", &obj);

  if (mrb_stat(mrb, obj, &fno) < 0)
    return mrb_false_value();
  if (!(fno.fattrib & AM_DIR))
    return mrb_true_value();

  return mrb_false_value();
}

/*
 * call-seq:
 *    File.zero?(file_name)   -> true or false
 *
 * Returns <code>true</code> if the named file exists and has
 * a zero size.
 */

mrb_value
mrb_filetest_s_zero_p(mrb_state *mrb, mrb_value klass)
{
  FILINFO fno;
  mrb_value obj;

  mrb_get_args(mrb, "o", &obj);

  if (mrb_stat(mrb, obj, &fno) < 0)
    return mrb_false_value();
  if (fno.fsize == 0)
    return mrb_true_value();

  return mrb_false_value();
}

/*
 * call-seq:
 *    File.size(file_name)   -> integer
 *
 * Returns the size of <code>file_name</code>.
 *
 * _file_name_ can be an IO object.
 */

mrb_value
mrb_filetest_s_size(mrb_state *mrb, mrb_value klass)
{
  FILINFO fno;
  mrb_value obj;

  mrb_get_args(mrb, "o", &obj);

  if (mrb_stat(mrb, obj, &fno) < 0)
    mrb_sys_fail(mrb, "mrb_stat");

  return mrb_fixnum_value(fno.fsize);
}

/*
 * call-seq:
 *    File.size?(file_name)   -> Integer or nil
 *
 * Returns +nil+ if +file_name+ doesn't exist or has zero size, the size of the
 * file otherwise.
 */

mrb_value
mrb_filetest_s_size_p(mrb_state *mrb, mrb_value klass)
{
  FILINFO fno;
  mrb_value obj;

  mrb_get_args(mrb, "o", &obj);

  if (mrb_stat(mrb, obj, &fno) < 0)
    return mrb_nil_value();
  if (fno.fsize == 0)
    return mrb_nil_value();

  return mrb_fixnum_value(fno.fsize);
}

void
mrb_init_file_test(mrb_state *mrb)
{
  struct RClass *f;

  f = mrb_define_class(mrb, "FileTest", mrb->object_class);

  mrb_define_class_method(mrb, f, "directory?", mrb_filetest_s_directory_p, MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, f, "exist?",     mrb_filetest_s_exist_p,     MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, f, "exists?",    mrb_filetest_s_exist_p,     MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, f, "file?",      mrb_filetest_s_file_p,      MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, f, "size",       mrb_filetest_s_size,        MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, f, "size?",      mrb_filetest_s_size_p,      MRB_ARGS_REQ(1));
  mrb_define_class_method(mrb, f, "zero?",      mrb_filetest_s_zero_p,      MRB_ARGS_REQ(1));
}
