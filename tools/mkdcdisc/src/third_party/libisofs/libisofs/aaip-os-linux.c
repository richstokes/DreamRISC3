
/*

 aaip-os-linux.c
 Arbitrary Attribute Interchange Protocol , system adapter for getting and
 setting of ACLs and xattr.

 To be included by aaip_0_2.c for Linux

 Copyright (c) 2009 - 2026 Thomas Schmitt

 This file is part of the libisofs project; you can redistribute it and/or
 modify it under the terms of the GNU General Public License version 2
 or later as published by the Free Software Foundation.
 See COPYING file for details.

*/

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


#ifdef Libisofs_with_aaip_acL
#include <sys/acl.h>
#endif

#ifdef Libisofs_with_aaip_xattR
#ifdef Libisofs_with_sys_xattR
#include <sys/xattr.h>
#else
#include <attr/xattr.h>
#endif
#endif

#ifdef Libisofs_include_ioctl_fs_H
#undef Libisofs_include_ioctl_fs_H
#endif
#ifdef Libisofs_with_aaip_lfa_flagS
#define Libisofs_include_ioctl_fs_H yes
#else
#ifdef Libisofs_with_aaip_projiD
#define Libisofs_include_ioctl_fs_H yes
#endif /* Libisofs_with_aaip_projiD */
#endif /* ! Libisofs_with_aaip_lfa_flagS */

#ifdef Libisofs_include_ioctl_fs_H
#include <sys/ioctl.h>
#include <linux/fs.h>
#endif
 

/* ------------------------------ Inquiry --------------------------------- */

/* See also API iso_local_attr_support().
   @param flag
        Bitfield for control purposes
             bit0= inquire availability of ACL
             bit1= inquire availability of xattr
             bit2= inquire availability of Linux-like file attribute flags
             bit3= inquire availability of XFS-style project id
             bit4 - bit7= Reserved for future types.
                          It is permissibile to set them to 1 already now.
             bit8 and higher: reserved, submit 0
   @return
        Bitfield corresponding to flag. If bits are set, th
             bit0= ACL adapter is enabled
             bit1= xattr adapter is enabled
             bit2= Linux-like file attribute flags adapter is enabled
             bit3= XFS-style project id is enabled
             bit4 - bit7= Reserved for future types.
             bit8 and higher: reserved, do not interpret these
*/
int aaip_local_attr_support(int flag)
{
 int ret= 0;

#ifdef Libisofs_with_aaip_acL
 if(flag & 1)
   ret|= 1;
#endif

#ifdef Libisofs_with_aaip_xattR
 if(flag & 2)
   ret|= 2;
#endif

#ifdef Libisofs_with_aaip_lfa_flagS
#ifdef FS_IOC_GETFLAGS
#ifdef FS_IOC_SETFLAGS
 if(flag & 4)
   ret|= 4;
#endif
#endif
#endif

#ifdef Libisofs_with_aaip_projiD
#ifdef FS_IOC_FSGETXATTR
#ifdef FS_IOC_FSSETXATTR
 if(flag & 8)
   ret|= 8;
#endif
#endif
#endif

 return(ret);
}


/* -------------------------- Error reporting ----------------------------- */

#ifdef Libisofs_aaip_local_erroR
#undef Libisofs_aaip_local_erroR
#endif

#ifdef Libisofs_with_aaip_xattR
#define Libisofs_aaip_local_erroR yes
#else
#ifdef Libisofs_with_aaip_acL
#define Libisofs_aaip_local_erroR yes
#else
#ifdef Libisofs_with_aaip_lfa_flagS
#ifdef FS_IOC_GETFLAGS
#define Libisofs_aaip_local_erroR yes
#else
#ifdef FS_IOC_SETFLAGS
#define Libisofs_aaip_local_erroR yes
#endif /* FS_IOC_SETFLAGS */
#endif /* ! FS_IOC_GETFLAGS */
#define Libisofs_aaip_local_erroR yes
#else
#ifdef Libisofs_with_aaip_projiD
#ifdef FS_IOC_FSGETXATTR
#define Libisofs_aaip_local_erroR yes
#endif /* FS_IOC_FSGETXATTR */
#endif /* ! Libisofs_with_aaip_projiD */
#endif /* ! Libisofs_with_aaip_lfa_flagS */
#endif /* ! Libisofs_with_aaip_acL */
#endif /* ! Libisofs_with_aaip_xattR */

#ifdef Libisofs_aaip_local_erroR

/* Report an error with local ACL or xattr calls.
   @param flag bit0-7: mode 0=NO_GET_LOCAL , 1=NO_SET_LOCAL
               bit8= WARNING instead of SORRY
*/
static
void aaip_local_error(char *function_name, char *path, int err, int flag)
{
 int mode, err_code;
 
 mode= (flag & 255);
 if(mode == 1) {
   if(flag & 256) {
     err_code= ISO_AAIP_NO_SET_LOCAL_W;
   } else {
     err_code= ISO_AAIP_NO_SET_LOCAL_S;
   }
 } else {
   if(flag & 256) {
     err_code= ISO_AAIP_NO_GET_LOCAL_W;
   } else {
     err_code= ISO_AAIP_NO_GET_LOCAL_S;
   }
 }
 if(err > 0) {
   if(path[0])
     iso_msg_submit(-1, err_code, 0,
                    "Function %s with file \"%s\" failed with errno %d '%s'",
                    function_name, path, err, strerror(err));
   else
     iso_msg_submit(-1, err_code, 0, "Function %s failed with %d '%s'",
                    function_name, err, strerror(err));
 } else {
   if(path[0])
     iso_msg_submit(-1, err_code, 0,
                    "Function %s with file \"%s\" failed without error code",
                    function_name, path);
   else
     iso_msg_submit(-1, err_code, 0,
                    "Function %s failed without error code",
                    function_name);
 }
}

#endif /* Libisofs_aaip_local_erroR */


/* ------------------------------ Getters --------------------------------- */

/* Obtain the ACL of the given file in long text form.
   @param path          Path to the file
   @param text          Will hold the result. This is a managed object which
                        finally has to be freed by a call to this function
                        with bit15 of flag.
   @param flag          Bitfield for control purposes
                        bit0=  obtain default ACL rather than access ACL
                               behave like bit4 if ACL is empty
                        bit4=  set *text = NULL and return 2
                               if the ACL matches st_mode permissions.
                        bit5=  in case of symbolic link: inquire link target
                        bit15= free text and return 1
   @return                1 ok
                          2 only st_mode permissions exist and bit 4 is set
                            or empty ACL and bit0 is set
                          0 ACL support not enabled at compile time
                            or filesystem does not support ACL
                         -1 failure of system ACL service (see errno)
                         -2 attempt to inquire ACL of a symbolic link without
                            bit4 or bit5 or with no suitable link target
*/
int aaip_get_acl_text(char *path, char **text, int flag)
{
#ifdef Libisofs_with_aaip_acL

 acl_t acl= NULL;
 struct stat stbuf;
 int ret;

 if(flag & (1 << 15)) {
   if(*text != NULL)
     acl_free(*text);
   *text= NULL;
   return(1);
 }
 *text= NULL;

 if(flag & 32)
   ret= stat(path, &stbuf);
 else
   ret= lstat(path, &stbuf);
 if(ret == -1)
   return(-1);
 if((stbuf.st_mode & S_IFMT) == S_IFLNK) {
   if(flag & 16)
     return(2);
   return(-2);
 }
 
 acl= acl_get_file(path, (flag & 1) ? ACL_TYPE_DEFAULT : ACL_TYPE_ACCESS);
 if(acl == NULL) {
   if(errno == ENOTSUP) {
     /* filesystem does not support ACL */
     if(flag & 16)
       return(2);
   
     /* >>> ??? fake ACL from POSIX permissions ? */;

     return(0);   
   }
   return(-1);
 }
 *text= acl_to_text(acl, NULL);
 acl_free(acl);

 if(*text == NULL)
   return(-1);
 if(flag & 16) {
   ret = aaip_cleanout_st_mode(*text, &(stbuf.st_mode), 2);
   if(!(ret & (7 | 64)))
     (*text)[0]= 0;
 }
 if(flag & (1 | 16)) {
   if((*text)[0] == 0 || strcmp(*text, "\n") == 0) {
     acl_free(*text);
     *text= NULL;
     return(2);
   }
 }
 return(1);

#else /* Libisofs_with_aaip_acL */

 return(0);
 
#endif /* ! Libisofs_with_aaip_acL */
}


#ifdef Libisofs_with_aaip_xattR

static int get_single_attr(char *path, char *name, size_t *value_length,
                           char **value_bytes, int flag)
{
 ssize_t value_ret;

 *value_bytes= NULL;
 *value_length= 0;
 if(flag & 32)
   value_ret= getxattr(path, name, NULL, 0);
 else
   value_ret= lgetxattr(path, name, NULL, 0);
 if(value_ret == -1) {
   aaip_local_error((flag & 32) ? "getxattr(2)" : "lgetxattr(2)", path, errno,
                    0);
   return(0);
 }
 *value_bytes= calloc(value_ret + 1, 1);
 if(*value_bytes == NULL)
   return(-1);
 if(flag & 32)
   value_ret= getxattr(path, name, *value_bytes, value_ret);
 else
   value_ret= lgetxattr(path, name, *value_bytes, value_ret);
 if(value_ret == -1) {
   aaip_local_error((flag & 32) ? "getxattr(2)" : "lgetxattr(2)", path, errno,
                    0);
   free(*value_bytes);
   *value_bytes= NULL;
   *value_length= 0;
   return(0);
 }
 *value_length= value_ret;
 return(1);
}

#endif /* Libisofs_with_aaip_xattR */


/* Obtain the Extended Attributes and/or the ACLs of the given file in a form
   that is ready for aaip_encode().
   @param path          Path to the file
   @param num_attrs     Will return the number of name-value pairs
   @param names         Will return an array of pointers to 0-terminated names
   @param value_lengths Will return an array with the lengths of values
   @param values        Will return an array of pointers to 8-bit values
   @param flag          Bitfield for control purposes
                        bit0=  obtain ACL (access and eventually default)
                        bit1=  use numeric ACL qualifiers rather than names
                        bit2=  do not obtain attributes other than ACL
                        bit3=  do not ignore eventual non-user attributes
                               I.e. those with a name which does not begin
                               by "user."
                        bit4=  do not return trivial ACL that matches st_mode
                        bit5=  in case of symbolic link: inquire link target
                        bit6=  do not obtain Linux style file attribute flags
                               (chattr)
                        bit7=  Without bit6: Ignore non-settable flags and do
                               not record "isofs.fa" if all flags are zero
                        bit8=  do not obtain XFS-style project id
                        bit15= free memory of names, value_lengths, values
   @return              1  ok
                        (reserved for FreeBSD: 2 ok, no permission to inspect
                                                 non-user namespaces.)
                        <=0 error
                        -1= out of memory
                        -2= program error with prediction of result size
                        -3= error with conversion of name to uid or gid
*/
int aaip_get_attr_list(char *path, size_t *num_attrs, char ***names,
                       size_t **value_lengths, char ***values, int flag)
{
 int ret;

#ifdef Libisofs_aaip_get_attr_activE
#undef Libisofs_aaip_get_attr_activE
#endif

#ifdef Libisofs_with_aaip_acL
 unsigned char *acl= NULL;
 char *a_acl_text= NULL, *d_acl_text= NULL;
 size_t acl_len= 0;
#ifndef Libisofs_aaip_get_attr_activE
#define Libisofs_aaip_get_attr_activE yes
#endif
#endif /* Libisofs_with_aaip_acL */

#ifdef Libisofs_with_aaip_xattR
 char *list= NULL;
 ssize_t value_ret, list_size= 0;
#ifndef Libisofs_aaip_get_attr_activE
#define Libisofs_aaip_get_attr_activE yes
#endif
#endif /* Libisofs_with_aaip_xattR */

#ifdef Libisofs_with_aaip_lfa_flagS
 uint64_t lfa_flags;
 int max_bit, os_errno, lfa_length;
 unsigned char lfa_value[8];
#ifndef Libisofs_aaip_get_attr_activE
#define Libisofs_aaip_get_attr_activE yes
#endif
#endif /* Libisofs_with_aaip_lfa_flagS */

#ifdef Libisofs_with_aaip_projiD
 uint32_t projid;
 int projid_os_errno, projid_length;
 unsigned char projid_value[8];
 int have_projid= 0;
#ifndef Libisofs_aaip_get_attr_activE
#define Libisofs_aaip_get_attr_activE yes
#endif
#endif /* Libisofs_with_aaip_projiD */

#ifdef Libisofs_aaip_get_attr_activE
 ssize_t i, num_names= 0;
#endif

 if(flag & (1 << 15)) { /* Free memory */
   {ret= 1; goto ex;}
 }

 *num_attrs= 0;
 *names= NULL;
 *value_lengths= NULL;
 *values= NULL;

#ifndef Libisofs_aaip_get_attr_activE

 ret = 1;
ex:;
 return ret;

#else /* Libisofs_aaip_get_attr_activE */

 /* Set up arrays */

#ifdef Libisofs_with_aaip_xattR

 if(!(flag & 4)) { /* Get xattr names */
    if(flag & 32)
      list_size= listxattr(path, list, 0);
    else
      list_size= llistxattr(path, list, 0);
    if(list_size == -1) {
      if(errno == ENOSYS) { /* Function not implemented */
        list_size= 0;     /* Handle as if xattr was disabled at compile time */
      } else {
        aaip_local_error((flag & 32) ? "listxattr(2)" : "llistxattr(2)", path,
                         errno, 0);
        {ret= -1; goto ex;}
      }
    }
    if(list_size > 0) {
      list= calloc(list_size, 1);
      if(list == NULL)
        {ret= -1; goto ex;}
      if(flag & 32)
        list_size= listxattr(path, list, list_size);
      else
        list_size= llistxattr(path, list, list_size);
      if(list_size == -1) {
        aaip_local_error((flag & 32) ? "listxattr(2)" : "llistxattr(2)", path,
                         errno, 0);
        {ret= -1; goto ex;}
      }
    }
    for(i= 0; i < list_size; i+= strlen(list + i) + 1)
      num_names++;
 }

#endif /* ! Libisofs_with_aaip_xattR */

#ifdef Libisofs_with_aaip_acL

 if(flag & 1)
   num_names++;

#endif

#ifdef Libisofs_with_aaip_lfa_flagS

 if(!(flag & 64))
   num_names++;

#endif

#ifdef Libisofs_with_aaip_projiD

 if(!(flag & 256)) {
   ret= iso_local_get_projid(path, &projid, &projid_os_errno, 0);
   if(ret > 0 && projid != 0) {
     num_names++;
     have_projid= 1;
   }
 }

#endif /* Libisofs_with_aaip_projiD */

 if(num_names == 0)
   {ret= 1; goto ex;}
 (*names)= calloc(num_names, sizeof(char *));
 (*value_lengths)= calloc(num_names, sizeof(size_t));
 (*values)= calloc(num_names, sizeof(char *));
 if(*names == NULL || *value_lengths == NULL || *values == NULL)
   {ret= -1; goto ex;}

 for(i= 0; i < num_names; i++) {
   (*names)[i]= NULL;
   (*values)[i]= NULL;
   (*value_lengths)[i]= 0;
 }

#ifdef Libisofs_with_aaip_xattR

 if(!(flag & 4)) { /* Get xattr values */
   for(i= 0; i < list_size && (size_t) num_names > *num_attrs;
       i+= strlen(list + i) + 1) {
     if(!(flag & 8))
       if(strncmp(list + i, "user.", 5))
   continue;
     (*names)[(*num_attrs)++]= strdup(list + i);
     if((*names)[(*num_attrs) - 1] == NULL)
       {ret= -1; goto ex;}
   }
   for(i= 0; (size_t) i < *num_attrs; i++) {
     if(!(flag & 8))
       if(strncmp((*names)[i], "user.", 5))
   continue;
     value_ret= get_single_attr(path, (*names)[i], *value_lengths + i,
                                *values + i, flag & 32);
     if(value_ret <= 0)
       {ret= -1; goto ex;}
   }
 }

#endif /* Libisofs_with_aaip_xattR */

#ifdef Libisofs_with_aaip_acL

 if(flag & 1) { /* Obtain ACL */

   aaip_get_acl_text(path, &a_acl_text, flag & (16 | 32));
   aaip_get_acl_text(path, &d_acl_text, 1 | (flag & 32));
   if(a_acl_text == NULL && d_acl_text == NULL)
     goto try_lfa_flags;
   ret= aaip_encode_both_acl(a_acl_text, d_acl_text, (mode_t) 0,
                             &acl_len, &acl, (flag & 2));
   if(ret <= 0)
     goto ex;

   /* Set as attribute with empty name */;
   (*names)[*num_attrs]= strdup("");
   if((*names)[*num_attrs] == NULL)
     {ret= -1; goto ex;}
   (*values)[*num_attrs]= (char *) acl;
   acl= NULL;
   (*value_lengths)[*num_attrs]= acl_len;
   (*num_attrs)++;
 }

try_lfa_flags:;

#endif /* Libisofs_with_aaip_acL */

#ifdef Libisofs_with_aaip_lfa_flagS

 if(!(flag & 64)) {
   /* ( aaip_get_lfa_flags() does not gracefully handle dead symbolic links) */
   ret= iso_local_get_lfa_flags(path, &lfa_flags, &max_bit, &os_errno,
                                flag & (1 << 7));
   if((flag & (1 << 7)) && lfa_flags == (uint64_t) 0) {
     /* virtually no lfa_flags because no settable ones */
     ret= 4;
   }
   if(ret == 1 || ret == 2) {
     ret= aaip_encode_uint64(lfa_flags, lfa_value, &lfa_length, 0);
     if(ret > 0) {
       (*names)[*num_attrs]= strdup("isofs.fa");
       if((*names)[*num_attrs] == NULL)
         {ret= -1; goto ex;}
       (*values)[*num_attrs]= calloc(lfa_length, 1);
       if((*values)[*num_attrs] == NULL)
         {ret= -1; goto ex;}
       memcpy((*values)[*num_attrs], (char *) lfa_value, lfa_length);
       (*value_lengths)[*num_attrs]= lfa_length;
       (*num_attrs)++;
     }
   }
 }

#endif /* Libisofs_with_aaip_lfa_flagS */

#ifdef Libisofs_with_aaip_projiD

 if(have_projid) {
   ret= iso_local_get_projid(path, &projid, &projid_os_errno, 0);
   if(ret > 0 && projid != 0) {
     /* Encode as big-endian number with no trailing 0-bytes */
     ret= aaip_encode_uint64((uint64_t) projid, projid_value, &projid_length,
                             0);
     if(ret > 0) {
       (*names)[*num_attrs]= strdup("isofs.pi");
       if((*names)[*num_attrs] == NULL)
         {ret= -1; goto ex;}
       (*values)[*num_attrs]= calloc(projid_length, 1);
       if((*values)[*num_attrs] == NULL)
         {ret= -1; goto ex;}
       memcpy((*values)[*num_attrs], (char *) projid_value, projid_length);
       (*value_lengths)[*num_attrs]= projid_length;
       (*num_attrs)++;
     }
   }
 }

#endif /* Libisofs_with_aaip_projiD */

 ret= 1;
ex:;
#ifdef Libisofs_with_aaip_acL
 if(a_acl_text != NULL)
   aaip_get_acl_text("", &a_acl_text, 1 << 15); /* free */
 if(d_acl_text != NULL)
   aaip_get_acl_text("", &d_acl_text, 1 << 15); /* free */
 if(acl != NULL)
   free(acl);
#endif
#ifdef Libisofs_with_aaip_xattR
 if(list != NULL)
   free(list);
#endif

 if(ret <= 0 || (flag & (1 << 15))) {
   if(*names != NULL) {
     for(i= 0; (size_t) i < *num_attrs; i++)
       free((*names)[i]);
     free(*names);
   }
   *names= NULL;
   if(*value_lengths != NULL)
     free(*value_lengths);
   *value_lengths= NULL;
   if(*values != NULL) {
     for(i= 0; (size_t) i < *num_attrs; i++)
       free((*values)[i]);
     free(*values);
   }
   *values= NULL;
   *num_attrs= 0;
 }
 return(ret);

#endif /* Libisofs_aaip_get_attr_activE */

}


/* Obtain the file attribute flags of the given file as bit array in uint64_t.
   The bit numbers are compatible to the FS_*_FL definitions in Linux
   include file <linux/fs.h>. A (possibly outdated) copy of them is in
   doc/susp_aaip_isofs_names.txt, name isofs.fa .
   The attribute flags of other systems may or may not be mappable to these
   flags.
   @param path          Path to the file
   @param lfa_flags     Will get filled with the FS_*_FL
   @param max_bit       Will tell the highest bit that is possibly set
                        (-1 = surely no bit is valid)
   @param os_errno      Will get filled with errno in case of error.
   @param flag          Bitfield for control purposes.
                        bit0= consider ENOTTY from FS_IOC_GETFLAGS an error
                              (else return 4 on ENOTTY)
                        bit2= do not issue own error messages with operating
                              system errors
                        bit7= Ignore non-settable flags
   @return              1= ok, all local attribute flags are in lfa_flags
                        2= ok, but some local flags could not be mapped to
                           the FS_*_FL bits
                        4= ok, ENOTTY from FS_IOC_GETFLAGS
                        0= local flag retrieval not enabled at compile time
                        <0 error with system calls:
                        -1= error with open(2)
                        -2= error with ioctl(2), not pardoned by bit0
*/
int aaip_get_lfa_flags(char *path, uint64_t *lfa_flags, int *max_bit,
                       int *os_errno, int flag)
{
 int ret= 0;
 static uint64_t user_settable= 0, su_settable= 0, non_settable= 0, unknown= 0;

#ifdef Libisofs_with_aaip_lfa_flagS
 int fd;
 long ioctl_result= 0;
#endif

 *lfa_flags= 0;
 *max_bit= -1;
 *os_errno= 0;

 if(non_settable == (uint64_t) 0)
   iso_util_get_lfa_masks(&user_settable, &su_settable, &non_settable,
                          &unknown);

#ifdef Libisofs_with_aaip_lfa_flagS
#ifdef FS_IOC_GETFLAGS
 fd= open(path, O_RDONLY | O_NDELAY);
 if(fd == -1) {
   if(!(flag & 4))
     aaip_local_error("open(2)", path, errno, 0);
   *os_errno= errno;
   return(-1);
 }
 ret= ioctl(fd, FS_IOC_GETFLAGS, &ioctl_result);
 close(fd);
 if(ret == -1) {
   if(errno == ENOTTY && !(flag & 1)) {
     /* Usual result with file type or filesystem without Linux attributes */
     *max_bit= 23;
     return(4);
   }
   if(!(flag & 4))
     aaip_local_error("ioctl(FS_IOC_GETFLAGS)", path, errno, 0);
   *os_errno= errno;
   return(-2);
 }
 *lfa_flags= ioctl_result;
 if(*lfa_flags < 1 << 24)
   *max_bit= 23;
 else if(*lfa_flags < (uint64_t) 1 << 32)
   *max_bit= 31;
 else
   *max_bit= sizeof(long) * 8 - 1;

 if(flag & (1 << 7))
   *lfa_flags&= ~non_settable;

 ret= 1;
   
#endif /* FS_IOC_GETFLAGS */
#endif /* Libisofs_with_aaip_lfa_flagS */

 return(ret);
}


/* Obtain the project id for XFS-style quota management.
   See man xfs_quota(8).
   @param path          Path to the file.
   @param projid        Will get filled with the project id.
   @param os_errno      Will get filled with errno in case of error.
   @param flag          Bitfield for control purposes.
                        bit2= do not issue own error messages with operating
                              system errors
   @return              1= ok, *projid is valid
                        0= local project id retrieval not enabled at compile 
                           time
                        <0 error with system calls:
                        -1= error with open(2)
                        -2= error with ioctl(2)
*/
int aaip_get_projid(char *path, uint32_t *projid, int *os_errno, int flag)
{
 int ret= 0;

#ifdef Libisofs_with_aaip_projiD
#ifdef FS_IOC_FSGETXATTR

 int fd;
 struct fsxattr ioctl_result;

#endif
#endif

 *projid= 0;

#ifdef Libisofs_with_aaip_projiD
#ifdef FS_IOC_FSGETXATTR

 fd= open(path, O_RDONLY | O_NDELAY);
 if(fd == -1) {
   aaip_local_error("open", path, errno, 0);
   *os_errno= errno;
   return(-1);
 }
 ret= ioctl(fd, FS_IOC_FSGETXATTR, &ioctl_result);
 close(fd);
 if(ret == -1) {
   if(errno == 25) {
     /* "Inappropriate ioctl for device" = projid not supported in fs
        Imitate ext4 behavior if projid is not enabled.
     */
     *projid= 0;
     return(1);
   }
   if(!(flag & 4))
     aaip_local_error("ioctl(FS_IOC_FSGETXATTR)", path, errno, 256);
   *os_errno= errno;
   return(-2);
 }
 *projid= ioctl_result.fsx_projid;
 ret= 1;

#endif /* FS_IOC_FSGETXATTR */
#endif /* Libisofs_with_aaip_lfa_flagS */

 return(ret);
}



/* ------------------------------ Setters --------------------------------- */


/* Set the ACL of the given file to a given list in long text form.
   @param path          Path to the file
   @param text          The input text (0 terminated, ACL long text form)
   @param flag          Bitfield for control purposes
                        bit0=  set default ACL rather than access ACL
                        bit5=  in case of symbolic link: manipulate link target
   @return              >0 ok
                         0 ACL support not enabled at compile time
                        -1 failure of system ACL service (see errno)
                        -2 attempt to manipulate ACL of a symbolic link
                           without bit5 or with no suitable link target
*/
int aaip_set_acl_text(char *path, char *text, int flag)
{

#ifdef Libisofs_with_aaip_acL

 int ret;
 acl_t acl= NULL;
 struct stat stbuf;

 if(flag & 32)
   ret= stat(path, &stbuf);
 else
   ret= lstat(path, &stbuf);
 if(ret == -1)
   return(-1);
 if((stbuf.st_mode & S_IFMT) == S_IFLNK)
   return(-2);

 acl= acl_from_text(text);
 if(acl == NULL) {
   aaip_local_error("acl_from_text(3)", "", errno, 1);
   ret= -1; goto ex;
 }
 ret= acl_set_file(path, (flag & 1) ? ACL_TYPE_DEFAULT : ACL_TYPE_ACCESS, acl);
 if(ret == -1) {
   aaip_local_error("acl_set_file(3)", path, errno, 1);
   goto ex;
 }
 ret= 1;
ex:
 if(acl != NULL)
   acl_free(acl);
 return(ret);

#else /* Libisofs_with_aaip_acL */

 return(0);

#endif /* ! Libisofs_with_aaip_acL */

}

#ifdef Libisofs_register_errnO
#undef Libisofs_register_errnO
#endif
#ifdef Libisofs_with_aaip_xattR
#define Libisofs_register_errnO yes
#else
#ifdef Libisofs_with_aaip_acL
#define Libisofs_register_errnO yes
#else
#endif /* ! Libisofs_with_aaip_acL */
#endif /* ! Libisofs_with_aaip_xattR */

#ifdef Libisofs_register_errnO

static void register_errno(int *errnos, int i)
{
 if(errno > 0)
   errnos[i]= errno;
 else
   errnos[i]= -1;
}

#endif /* Libisofs_register_errnO */


/* Bring the given attributes and/or ACLs into effect with the given file.
   @param flag          Bitfield for control purposes
                        bit0= decode and set ACLs
                        bit1= first clear all existing attributes of the file
                        bit2= do not set attributes other than ACLs
                        bit3= do not ignore eventual non-user attributes.
                              I.e. those with a name which does not begin
                              by "user."
                        bit5= in case of symbolic link: manipulate link target
                        bit6= tolerate inappropriate presence or absence of
                              directory default ACL
                        bit7= avoid setting a name value pair if it already
                              exists and has the desired value.
   @return              1 success
                       -1 error memory allocation
                       -2 error with decoding of ACL
                       -3 error with setting ACL
                       -4 error with setting attribute
                       -5 error with deleting attributes
                       -6 support of xattr not enabled at compile time
                       -7 support of ACL not enabled at compile time
                     ( -8 unsupported xattr namespace )
    ISO_AAIP_ACL_MULT_OBJ multiple entries of user::, group::, other::
*/
int aaip_set_attr_list(char *path, size_t num_attrs, char **names,
                       size_t *value_lengths, char **values,
                       int *errnos, int flag)
{
 int ret, end_ret= 1;
 size_t i, consumed, acl_text_fill, acl_idx= 0;
 char *acl_text= NULL;
#ifdef Libisofs_with_aaip_xattR
 char *list= NULL, *old_value;
 ssize_t list_size= 0, value_ret;
 size_t old_value_l;
 int skip;
#endif
#ifdef Libisofs_with_aaip_acL
 size_t h_consumed;
 int has_default_acl= 0;
#endif

 for(i= 0; i < num_attrs; i++)
   errnos[i]= 0;

#ifdef Libisofs_with_aaip_xattR

 if(flag & 2) { /* Delete all file attributes */
   if(flag & 32)
     list_size= listxattr(path, list, 0);
   else
     list_size= llistxattr(path, list, 0);
 }
 if(list_size > 0) { /* Delete all file attributes */
   list= calloc(list_size, 1);
   if(list == NULL)
     {ret= -5; goto ex;}
   if(flag & 32)
     list_size= listxattr(path, list, list_size);
   else
     list_size= llistxattr(path, list, list_size);
   if(list_size == -1) {
     aaip_local_error((flag & 32) ? "listxattr(2)" : "llistxattr(2)", path,
                      errno, 1);
     {ret= -5; goto ex;}
   }
   for(i= 0; i < (size_t) list_size; i+= strlen(list + i) + 1) {
      if(!(flag & 8))
        if(strncmp(list + i, "user.", 5))
   continue;
     if(flag & 32)
       ret= removexattr(path, list + i);
     else
       ret= lremovexattr(path, list + i);
     if(ret == -1) {
       aaip_local_error((flag & 32) ? "removexattr(2)" : "lremovexattr(2)",
                        path, errno, 1);
       {ret= -5; goto ex;}
     }
   }
   free(list); list= NULL;
 }

#endif /* Libisofs_with_aaip_xattR */

 for(i= 0; i < num_attrs; i++) {
   if(names[i] == NULL || values[i] == NULL)
 continue;
   if(names[i][0] == 0) { /* ACLs */
     if(flag & 1)
       acl_idx= i + 1;
 continue;
   }
   /* Extended Attribute */
   if(flag & 4)
 continue;
   if(strncmp(names[i], "isofs.", 6) == 0)
 continue;
   if(!(flag & 8))
     if(strncmp(names[i], "user.", 5))
 continue;

#ifdef Libisofs_with_aaip_xattR

   skip= 0;
   if(flag & 128) {
     value_ret= get_single_attr(path, names[i], &old_value_l,
                                &old_value, flag & 32);
     if(value_ret > 0 && old_value_l == value_lengths[i]) {
       if(memcmp(old_value, values[i], value_lengths[i]) == 0)
         skip= 1;
     }
     if(old_value != NULL)
       free(old_value);
   }
   if(!skip) {
     if(flag & 32)
       ret= setxattr(path, names[i], values[i], value_lengths[i], 0);
     else
       ret= lsetxattr(path, names[i], values[i], value_lengths[i], 0);
     if(ret == -1) {
       aaip_local_error((flag & 32) ? "setxattr(2)" : "lsetxattr(2)", path,
                        errno, 1);
       register_errno(errnos, i);
       end_ret= -4;
 continue;
     }
   }

#else

   {ret= -6; goto ex;}

#endif /* ! Libisofs_with_aaip_xattR */

 }

 /* Decode ACLs */
 /* It is important that this happens after restoring xattr which might be
    representations of ACL, too. If isofs ACL are enabled, then they shall
    override the xattr ones.
 */
 if(acl_idx == 0)
   {ret= end_ret; goto ex;}
 i= acl_idx - 1;
                                                             /* "access" ACL */
 ret= aaip_decode_acl((unsigned char *) values[i], value_lengths[i],
                      &consumed, NULL, 0, &acl_text_fill, 1);
 if(ret < -3)
   goto ex;
 if(ret <= 0)
   {ret= -2; goto ex;}
 acl_text= calloc(acl_text_fill, 1);
 if(acl_text == NULL)
   {ret= -1; goto ex;}
 ret= aaip_decode_acl((unsigned char *) values[i], value_lengths[i],
                   &consumed, acl_text, acl_text_fill, &acl_text_fill, 0);
 if(ret < -3)
   goto ex;
 if(ret <= 0)
   {ret= -2; goto ex;}

#ifdef Libisofs_with_aaip_acL

 has_default_acl= (ret == 2);

 ret= aaip_set_acl_text(path, acl_text, flag & 32);
 if(ret == -1)
   register_errno(errnos, i);
 if(ret <= 0)
   {ret= -3; goto ex;}
                                                            /* "default" ACL */
 if(has_default_acl) {
   free(acl_text);
   acl_text= NULL;
   ret= aaip_decode_acl((unsigned char *) (values[i] + consumed),
                        value_lengths[i] - consumed, &h_consumed,
                        NULL, 0, &acl_text_fill, 1);
   if(ret < -3)
     goto ex;
   if(ret <= 0)
     {ret= -2; goto ex;}
   acl_text= calloc(acl_text_fill, 1);
   if(acl_text == NULL)
     {ret= -1; goto ex;}
   ret= aaip_decode_acl((unsigned char *) (values[i] + consumed),
                        value_lengths[i] - consumed, &h_consumed,
                        acl_text, acl_text_fill, &acl_text_fill, 0);
   if(ret < -3)
     goto ex;
   if(ret <= 0)
     {ret= -2; goto ex;}
   ret= aaip_set_acl_text(path, acl_text, 1 | (flag & 32));
   if(ret == -1)
     register_errno(errnos, i);
   if(ret <= 0)
     {ret= -3; goto ex;}
 } else {
   if(!(flag & 64)) {

     /* >>> ??? take offense from missing default ACL ?
       ??? does Linux demand a default ACL for directories with access ACL ?
      */;

   }
 }
 ret= end_ret;

#else

 ret= -7;

#endif /* !Libisofs_with_aaip_acL */

ex:;
 if(acl_text != NULL)
   free(acl_text);

#ifdef Libisofs_with_aaip_xattR
 if(list != NULL)
   free(list);
#endif

 return(ret);
}


/*
   @param flag          Bitfield for control purposes.
                        bit2= do not issue own error messages with operating
                              system errors
   @return              1= ok, all lfa_flags bits were written
                        2= ok, but some FS_*_FL bits could not be mapped to
                           local flags
                        0= local flags setting not enabled at compile time
                        <0 error with system calls or with max_bit:
                        -1= error with open(2)
                        -2= error with ioctl(2)
                        -3= error with max_bit
*/
int aaip_set_lfa_flags(char *path, uint64_t lfa_flags, int max_bit,
                       int *os_errno, int flag)
{
 int ret= 0;

#ifdef Libisofs_with_aaip_lfa_flagS
 int fd;
 long ioctl_arg;
#endif

 *os_errno= 0;

#ifdef Libisofs_with_aaip_lfa_flagS
#ifdef FS_IOC_SETFLAGS

 if(max_bit > (int) sizeof(long) * 8 - 1) {
   aaip_local_error("ioctl(FS_IOC_SETFLAGS) with too many bits", path, 0, 0);
   return(-3);
 }
   
 fd= open(path, O_RDONLY | O_NDELAY);
 if(fd == -1) {
   if(!(flag & 4))
     aaip_local_error("open(2)", path, errno, 0);
   *os_errno= errno;
   return(-1);
 }
 if(max_bit < 0)
   ioctl_arg= 0;
 else
   ioctl_arg= lfa_flags;
 ret= ioctl(fd, FS_IOC_SETFLAGS, &ioctl_arg);
 close(fd);
 if(ret == -1) {
   if(!(flag & 4))
     aaip_local_error("ioctl(FS_IOC_SETFLAGS)", path, errno, 0);
   *os_errno= errno;
   return(-2);
 }
 ret= 1;
   
#endif /* FS_IOC_SETFLAGS */
#endif /* Libisofs_with_aaip_lfa_flagS */

 return(ret);
}


/* Set the project id for XFS-style quota management.
   @param path          Path to the file.
   @param projid        Contains the project id for the file.
   @param os_errno      Will get filled with errno in case of error.
   @param flag          Bitfield for control purposes.
                        bit2= do not issue own error messages with operating
                              system errors
   @return              1= ok, projid was written
                        0= local flags setting not enabled at compile time
                        -1= error with open(2)
                        -2= error with ioctl(FS_IOC_FSGETXATTR)
                        -3= error with ioctl(FS_IOC_FSSETXATTR)
*/
int aaip_set_projid(char *path, uint32_t projid, int *os_errno, int flag)
{
 int ret= 0;

#ifdef Libisofs_with_aaip_projiD
#ifdef FS_IOC_FSGETXATTR
#ifdef FS_IOC_FSSETXATTR

 int fd;
 struct fsxattr ioctl_arg;

 fd= open(path, O_RDONLY | O_NDELAY);
 if(fd == -1) {
   if(!(flag & 4))
     aaip_local_error("open", path, errno, 0);
   *os_errno= errno;
   return(-1);
 }
 ret= ioctl(fd, FS_IOC_FSGETXATTR, &ioctl_arg);
 if(ret == -1) {
   if(!(flag & 4))
     aaip_local_error("ioctl(FS_IOC_FSGETXATTR)", path, errno, 0);
   *os_errno= errno;
   close(fd);
   return(-2);
 }
 ioctl_arg.fsx_projid= projid;
 ret= ioctl(fd, FS_IOC_FSSETXATTR, &ioctl_arg);
 close(fd);
 if(ret == -1) {
   if(!(flag & 4))
     aaip_local_error("ioctl(FS_IOC_FSSETXATTR)", path, errno, 0);
   *os_errno= errno;
   return(-3);
 }
 ret= 1;

#endif /* FS_IOC_FSSETXATTR */
#endif /* FS_IOC_FSGETXATTR */
#endif /* Libisofs_with_aaip_lfa_flagS */

 return(ret);
}


/* -------- API for creating device files in the local filesystem --------- */


/* API */
/* @param flag  bit0= do not issue error messages
*/
int iso_local_create_dev(char *disk_path, mode_t st_mode, dev_t dev,
                         int *os_errno, int flag)
{
 int ret;

 *os_errno= 0;
 if((st_mode & S_IFMT) != S_IFBLK && (st_mode & S_IFMT) != S_IFCHR) {
   if(!(flag & 1))
     iso_msg_submit(-1, ISO_DEV_NO_CREATION, 0,
"Device file \"%s\" cannot be created because not of type S_IFBLK or S_IFCHR",
                    disk_path);
   return ISO_DEV_NO_CREATION;
 }
 st_mode&= (S_IFMT | 07777);
 ret= mknod(disk_path, st_mode, dev);
 if(ret == -1) {
   *os_errno= errno;
   if(!(flag & 1)) {
     if(errno > 0) {
       iso_msg_submit(-1, ISO_DEV_NOT_CREATED, 0,
                      "Creation of device file \"%s\" failed with %d '%s'",
                      disk_path, errno, strerror(errno));
     } else {
       iso_msg_submit(-1, ISO_DEV_NOT_CREATED, 0,
                    "Creation of device file \"%s\" failed without error code",
                      disk_path);
     }
   }
   return ISO_DEV_NOT_CREATED;
 }
 return(ISO_SUCCESS);
}



