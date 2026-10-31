
/*

 aaip-os-dummy.c

 Idle placeholder for:
 Arbitrary Attribute Interchange Protocol , system adapter for getting and
 setting of ACLs and xattr.

 See aaip-os-linux.c for a real implementation of this interface.

 To be included by aaip_0_2.c

 Copyright (c) 2009 - 2025 Thomas Schmitt

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
/* mkdcdisc-patch: portability: MinGW-w64 needs stdint.h for the
 * fixed-width types used below. */
#ifdef _WIN32
#include <stdint.h>
#endif
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>


/* ------------------------------ Inquiry --------------------------------- */

/* See also API iso_local_attr_support().
   @param flag
        Bitfield for control purposes
             bit0= inquire availability of ACL
             bit1= inquire availability of xattr
             bit2 - bit7= Reserved for future types.
                          It is permissibile to set them to 1 already now.
             bit8 and higher: reserved, submit 0
   @return
        Bitfield corresponding to flag.
             bit0= ACL adapter is enabled
             bit1= xattr adapter is enabled
             bit2= Linux-like file attribute flags (chattr) adapter is enabled
             bit3= inquire availability of XFS-style project id
             bit4 - bit7= Reserved for future types.
             bit8 and higher: reserved, do not interpret these
*/
int aaip_local_attr_support(int flag)
{
 return(0);
}


/* ------------------------------ Getters --------------------------------- */

/* Obtain the ACL of the given file in long text form.
   @return          0 ACL support not enabled at compile time
*/
int aaip_get_acl_text(char *path, char **text, int flag)
{
 return(0);
}


/* Obtain the Extended Attributes and/or the ACLs of the given file in a form
   that is ready for aaip_encode().
   @return              1   ok
*/
int aaip_get_attr_list(char *path, size_t *num_attrs, char ***names,
                       size_t **value_lengths, char ***values, int flag)
{
 *num_attrs= 0;
 *names= NULL;
 *value_lengths= NULL;
 *values= NULL;
 return(1);
}


/* Obtain the file attribute flags of the given file as bit array in uint64_t.
   The bit numbers are compatible to the FS_*_FL definitions in Linux.
*/
int aaip_get_lfa_flags(char *path, uint64_t *lfa_flags, int *max_bit,
                       int *os_errno, int flag)
{
 *lfa_flags= 0;
 *max_bit= -1;
 *os_errno= 0;
 return(0);
}


/* Obtain the project id for XFS-style quota management.
*/
int aaip_get_projid(char *path, uint32_t *projid, int *os_errno, int flag)
{
 *projid= 0;
 *os_errno= 0;
 return(0);
}


/* ------------------------------ Setters --------------------------------- */


/* Set the ACL of the given file to a given list in long text form.
   @return              0 ACL support not enabled at compile time
*/
int aaip_set_acl_text(char *path, char *text, int flag)
{
 return(0);
}


/* Bring the given attributes and/or ACLs into effect with the given file.
   @param flag          Bitfield for control purposes
                        bit0= decode and set ACLs
                        bit1= first clear all existing attributes of the file
                        bit2= do not set attributes other than ACLs
   @return              1 success (there was nothing to do)
                       -6 support of xattr not enabled at compile time
                       -7 support of ACL not enabled at compile time
*/
int aaip_set_attr_list(char *path, size_t num_attrs, char **names,
                       size_t *value_lengths, char **values, int *errnos,
                       int flag)
{
 size_t i;

 for(i= 0; i < num_attrs; i++)
   errnos[i]= 0;

 for(i= 0; i < num_attrs; i++) {
   if(names[i] == NULL || values[i] == NULL)
 continue;
   if(names[i][0] == 0) { /* ACLs */
     if(flag & 1)
       return(-7);
 continue;
   }
   /* Extended Attribute */
   if(flag & 4)
 continue;
   if(!(flag & 8))
     if(strncmp(names[i], "user.", 5))
 continue;
   return(-6);
 }
 if(flag & 2)
   return(-6);
 return(1);
}


int aaip_set_lfa_flags(char *path, uint64_t lfa_flags, int max_bit,
                       int *os_errno, int flag)
{
 *os_errno= 0;
 return(0);
}


/* Set the project id for XFS-style quota management.
*/
int aaip_set_projid(char *path, uint32_t projid, int *os_errno, int flag)
{
 *os_errno= 0;
 return(0);
}



/* -------- API for creating device files in the local filesystem --------- */


/* API */
/* @param flag  bit0= do not issue error messages
*/
int iso_local_create_dev(char *disk_path, mode_t st_mode, dev_t dev,
                         int *os_errno, int flag)
{
 /* From man mknod:
    POSIX.1-2001 says: "The only portable use of mknod()  is  to  create  a
    FIFO-special  file.  If mode is not S_IFIFO or dev is not 0, the behav‐
    ior of mknod() is unspecified."
 */
 *os_errno= 0;
 if(!(flag & 1))
   iso_msg_submit(-1, ISO_DEV_NO_CREATION, 0,
   "File \"%s\" cannot be created because device file creation is not enabled",
                  disk_path);
 return ISO_DEV_NO_CREATION;
}

