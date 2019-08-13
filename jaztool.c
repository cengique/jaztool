/* 
 * jaztool.c --
 *
 *	Linux Tool for the Iomega JAZ/ZIP Drives.
 *
 * This program uses SCSI_IOCTL_SEND_COMMAND to deliver vendor specific 
 * commands to an Iomega JAZ/ZIP drive.  The program attempts to ensure that
 * the SCSI device is actually a JAZ/ZIP drive and that it is not currently
 * mounted.  The checks are *not* foolproof - but only root can do these
 * things anyway !
 * 
 * Usage:  jaztool <dev> <command>
 * 
 * where <dev> must be the full name of a scsi device, eg: /dev/sdc, note
 * that you must not specify a partition on the device.  <command> can
 * be one of:
 * 
 *	eject	ejects the disk in the drive
 *	ro	puts the disk into read-only mode, and ejects
 *	rw      puts the disk into read-write mode, and ejects  
 *	status  prints the read/write status of the disk
 * 
 * If the disk is in a password protected mode, you will be prompted for
 * a password when you attempt to change the mode.  This version of 
 * jaztool does not officially support putting the disk into the password
 * protected read-only mode, but the command 'PWRO' will do this if you
 * are really sure that you want to.  REMEMBER: if you forget the password,
 * you will not be able to put the disk back into read-write mode.
 * 
 * NOTE: The JAZ/ZIP drives also support a protection mode 5 'PWNR',
 * under which the disk is neither readable nor writable until it is
 * unlocked with a password.  This program could not previously unlock
 * a JAZ disk in mode 5 - as Linux 2.x is unable to open a disk that
 * is not readable.  To support this feature would require patching
 * the Linux kernel.  (And therefore, there is no command to put a
 * disk into this mode, either.) However, as of 2019, and Linux 4.x
 * USB ZIP drives are able to be unlocked from mode 5 using this
 * program.
 *   
 * Whenever you change the write-protection mode, jaztool ejects the
 * disk.  This is done to ensure that Linux will recheck the mode
 * before it attempts to use the disk again. There is a switch -n if
 * you prefer not to eject the disk after the operation. *
 *
 * (c) 1996 - Bob Willmot
 * bwillmot@cnct.com
 * http://www.cnct.com/~bwillmot/
 *
 * adapted from work for the Iomega Zip by Grant R. Guenther 
 * and Itai Nahshon
 *
 * 2019/08 - Modified by Cengiz Gunay <cengique@users.sf.net>
 * - Added support for IOMEGA ZIP 250 drive
 * - Increased verbosity of error messages
 *
 */

static char rcsid[] = "$Id: jaztool.c,v 1.3 1996/07/27 16:30:45 root Exp $";

#include <stdio.h>
#include <mntent.h>


/* 
 * this is ugly and should not be here - but the SCSI include files are
 * moving around ...
 */
#define SCSI_IOCTL_SEND_COMMAND 1

struct sdata {
  int  inlen;
  int  outlen;
  char cmd[256];
} scsi_cmd;

/*
 * Drive identifier
 */
char	id[25] = "\0";

/*
 * Debug switch
 */
unsigned char no_eject = 0;


/*
 *----------------------------------------------------------------------
 *
 * error --
 *
 *	print error and exit
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	exits program with errorlevel 1
 *
 *----------------------------------------------------------------------
 */
void error(char * msg)
{ 	
  printf("jaztool %s: %s\n", id, msg);
  exit(1);
}


/*
 *----------------------------------------------------------------------
 *
 * is_mounted --
 *
 *	determine if the disk is mounted
 *
 * Results:
 *	-1 = error,  0 = not mounted,  1 = mounted
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int 
is_mounted( char * fs )
{  
  struct mntent *mp;
  FILE *mtab;
  
  mtab = setmntent("/etc/mtab","r");
  if (!mtab) return -1;
  while (mp=getmntent(mtab)) 
    if (!strncmp(mp->mnt_fsname,fs,8))
      break;
  endmntent(mtab);
  return (mp != NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * is_raw_scsi --
 *
 *	determine if the given device is a raw SCSI device
 *
 * Results:
 *	1 = true
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int 
is_raw_scsi( char * fs )
{	
  return ((strlen(fs) == 8) && (strncmp("/dev/sd",fs,7) == 0));
}


/*
 *----------------------------------------------------------------------
 *
 * is_jaz --
 *
 *	determine if device is a Jaz
 *
 * Results:
 *	1 = true
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
is_jaz( int fd )
{	
  int	i;
  
  scsi_cmd.inlen = 0;
  scsi_cmd.outlen = 40;
  scsi_cmd.cmd[0] = 0x12;		/* inquiry */
  scsi_cmd.cmd[1] = 0;
  scsi_cmd.cmd[2] = 0;
  scsi_cmd.cmd[3] = 0;
  scsi_cmd.cmd[4] = 40;
  scsi_cmd.cmd[5] = 0;
  
  if (ioctl(fd,SCSI_IOCTL_SEND_COMMAND,(void *)&scsi_cmd))
    error("inquiry ioctl error");
  
  for(i=0;i<24;i++) {
    id[i] = scsi_cmd.cmd[i+8];
  }
  /*
   * Truncate at 15 since the two drives we accept are only 15 chars
   * long, although it could go until 24. May need to change this in
   * future.
   */
  id[15] = 0;

  /* 
   * compare the string case insensitive
   * just in case Iomega changes the
   * BIOS information of future drives
   */
  return(!strncasecmp(id,"IOMEGA  JAZ 1GB",15) || !strncasecmp(id,"IOMEGA  ZIP 250",15));
}


/*
 *----------------------------------------------------------------------
 *
 * motor --
 *
 *	use SCSI_IOCTL_SEND_COMMAND to send a command to the scsi device
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void	
motor( int fd, int mode )
{	
  scsi_cmd.inlen = 0;
  scsi_cmd.outlen = 0;
  scsi_cmd.cmd[0] = 0x1b;		/* start/stop */
  scsi_cmd.cmd[1] = 0;
  scsi_cmd.cmd[2] = 0;
  scsi_cmd.cmd[3] = 0;
  scsi_cmd.cmd[4] = mode;
  scsi_cmd.cmd[5] = 0;

  if (ioctl(fd,SCSI_IOCTL_SEND_COMMAND,(void *)&scsi_cmd))
    error("motor control ioctl error");
}


/*
 *----------------------------------------------------------------------
 *
 * unlockdook --
 *
 *	send an unlock command to the scsi device
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void	
unlockdoor( int fd )
{	
  scsi_cmd.inlen = 0;
  scsi_cmd.outlen = 0;
  scsi_cmd.cmd[0] = 0x1e;		/* prevent/allow media removal */
  scsi_cmd.cmd[1] = 0;
  scsi_cmd.cmd[2] = 0;
  scsi_cmd.cmd[3] = 0;
  scsi_cmd.cmd[4] = 0;
  scsi_cmd.cmd[5] = 0;

  if (ioctl(fd,SCSI_IOCTL_SEND_COMMAND,(void *)&scsi_cmd))
    error("door unlock ioctl error");
}


/*
 *----------------------------------------------------------------------
 *
 * eject --
 *
 *	eject the disk
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void 
eject ( int fd )
{	
  unlockdoor(fd);
  motor(fd,1);
  motor(fd,2);
}


/*
 *----------------------------------------------------------------------
 *
 * get_prot_mode --
 *
 *	get the protection mode of the drive
 *
 * Results:
 *	protection code - see dostatuus()
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
get_prot_mode( int fd )
{      	
  scsi_cmd.inlen = 0;
  scsi_cmd.outlen = 256;
  scsi_cmd.cmd[0] = 6;		/* Iomega non-sense command */
  scsi_cmd.cmd[1] = 0;
  scsi_cmd.cmd[2] = 2;
  scsi_cmd.cmd[3] = 0;
  scsi_cmd.cmd[4] = 128;
  scsi_cmd.cmd[5] = 0;

  if (ioctl(fd,SCSI_IOCTL_SEND_COMMAND,(void *)&scsi_cmd))
    error("non-sense ioctl error");

  /* 
   *see dostatus() for protection codes
   */
  return (scsi_cmd.cmd[21] & 0x0f);	
}


/*
 *----------------------------------------------------------------------
 *
 * dostatus --
 *
 *	explain protection codes
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void  
dostatus( int fd, char * dev )
{	
  int	s;

  s = get_prot_mode(fd);

  if (s == 0) printf("jaztool %s: %s is not write-protected\n",id,dev);
  else if (s == 2) printf("jaztool %s: %s is write-protected\n",id,dev);
  else if (s == 3) printf("jaztool %s: %s is password write-protected\n",id,dev);
  else if (s == 5) printf("jaztool %s: %s is password read/write-protected\n",id,dev);
  else printf("jaztool %s: %s status %d unknown\n",id,dev, s);

  /* status 13 ? */
}


/*
 *----------------------------------------------------------------------
 *
 * pmode --
 *
 *	change protection mode
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void  
pmode ( int fd, int mode, char * dev)
{	
  int	i, oldmode, len;
  char	pw[34];
  
  pw[0]=0;
  oldmode = get_prot_mode(fd);
  if ((1 & mode) || (1 & oldmode)) {
    printf("Password: ");
    fgets(pw,33,stdin);
    len = strlen(pw);
    if ((len > 0) && (pw[len-1] == '\n')) pw[len-1] = 0;
  }
  len = strlen(pw);
  
  scsi_cmd.inlen = len;
  scsi_cmd.outlen = 0;
  scsi_cmd.cmd[0] = 0x0c;
  scsi_cmd.cmd[1] = mode;
  scsi_cmd.cmd[2] = 0;
  scsi_cmd.cmd[3] = 0;
  scsi_cmd.cmd[4] = len;
  scsi_cmd.cmd[5] = 0;
  
  for(i=0;i<len;i++) {
    scsi_cmd.cmd[6+i] = pw[i];
  }
  
  if (ioctl(fd,SCSI_IOCTL_SEND_COMMAND,(void *)&scsi_cmd))
    error("set protection mode ioctl error - wrong password?");
  
  dostatus(fd,dev);
  
  /* 
   * whenever the protection is changes
   * the disk must be ejected 
   * so linux can reset the wp mode 
   */
  if (!no_eject)
    eject(fd);	
}


/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *	the whole ball 'o' wax
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	depends on the switches :-)
 *
 *----------------------------------------------------------------------
 */
main(int argc, char **argv)
{  	
  int   rs;
  int   jfd;

  if (argc < 3 || argc > 4) {
    printf("usage: jaztool /dev/sd? eject|ro|rw|status [-n]\n");
    printf("\t-n: no eject after operations\n");
    exit(1);
  }
  if (!is_raw_scsi(argv[1])) error("not a raw scsi device");
  rs = is_mounted(argv[1]);
  if (rs == -1) error("unable to access /etc/mtab");
  if (rs == 1) error("device is mounted");

  jfd = open(argv[1],0);
  if (jfd < 0) error("unable to open device");

  if (!is_jaz(jfd)) {
    error("not a known IOMEGA JAZ/ZIP drive");
  }

  if (argc > 3 && !strcmp(argv[3],"-n"))
    no_eject = 1;
  
  if (!strcmp(argv[2],"eject")) eject(jfd);
  else if (!strcmp(argv[2],"ro")) pmode(jfd,2,argv[1]);
  else if (!strcmp(argv[2],"rw")) pmode(jfd,0,argv[1]);
  /*  else if (!strcmp(argv[2],"RW")) pmode(jfd,8,argv[1]); */ /* testing - unprotect until eject? */
  else if (!strcmp(argv[2],"PWRO")) pmode(jfd,3,argv[1]);
  else if (!strcmp(argv[2],"PWNR")) pmode(jfd,5,argv[1]); /* password protect read/ */
  else if (!strcmp(argv[2],"status")) dostatus(jfd,argv[1]);
  else error("unknown command");

  close(jfd);
}
