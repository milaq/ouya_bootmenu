/*
** BootMenu - Boot Image Menu for the Ouya
**
** Compile for ARM on desktop Linux:
**     arm-linux-gnueabihf-gcc -Os bootmenu.c -o bootmenu  \
**         -static -Wl,--gc-sections -Wl,--strip-all
*/

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#include "roboto_23x41.h"


#define FALSE		(0)
#define TRUE		(1)

#define MAX_MENU_SEL	(5)

#define BPP		(32 / 8)		/* bytes/pixel for 32 bpp */
#define FB_SIZE		(BPP * xres * yres)

#define MAX_IMG_SIZE	(8 * 1024 * 1024)	/* Android kernel file */

#define MOUNT_PATH	"/dev/block/platform/sdhci-tegra.3/by-name"


void alarm_handler(int sig) { fprintf(stderr, "watchdog expired, booting fallback\n"); exit(1); }

int main(int argc, char *argv[])
{
	int				ret;
	int				x, y;
	int				xres, yres;
	int				countdown = 5/*sec*/ * 10;
	int				menu_sel = 1, menu_old = -1;
	int				fb_fd, ev_fd[3];
	unsigned char			*fontmap, *fontmap_, *runptr, runchar;
        struct input_event		in_ev;
	struct fb_fix_screeninfo	fb_fi;
	struct fb_var_screeninfo	fb_vi;
	char				*fb_map_buf;
	static char			fb_tmp1_buf[BPP * 3840 * 2160],
					fb_tmp2_buf[BPP * 3840 * 2160];
	char				*img_pname = "", *img2_pname = "";

	fprintf(stderr, "bootmenu startup\n");
	signal(SIGALRM, alarm_handler);		/* watchdog */
	alarm(60);

	/*
	** Open the HID device files so that we can monitor the power
	** button.  We're not sure which file, so open the first three.
	*/
	ev_fd[0] = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
	ev_fd[1] = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
	ev_fd[2] = open("/dev/input/event2", O_RDONLY | O_NONBLOCK);

	/*
	** Uncompress font.  We end up with a byte-based multi-char bitmap.
	*/
	fontmap = fontmap_ = malloc(font.width * font.height);
	runptr = font.rundata;
	while ((runchar = *runptr++) != 0)  {
		memset(fontmap_, (runchar & 0x80) ? 0xff : 0, runchar & 0x7f);
		fontmap_ += (runchar & 0x7f);
	}

	/*
	** Open framebuffer and get settings.
	*/
	memset(&fb_vi, 0, sizeof(fb_vi));
	memset(&fb_fi, 0, sizeof(fb_fi));
	fb_fd = open("/dev/graphics/fb0", O_RDWR);
	ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_vi);
	xres = fb_vi.xres;  yres = fb_vi.yres;
	fb_vi.activate       = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
	fb_vi.bits_per_pixel = 32;
	ioctl(fb_fd, FBIOPUT_VSCREENINFO, &fb_vi);
	ioctl(fb_fd, FBIOGET_FSCREENINFO, &fb_fi);
	fprintf(stderr, "fb: smem_len=%d, bpp=%d, xres=%d, yres=%d\n",
		fb_fi.smem_len, fb_vi.bits_per_pixel, xres, yres);
	if (fb_fi.smem_len >= 1)
		fb_map_buf = mmap(NULL, fb_fi.smem_len,
				  PROT_READ|PROT_WRITE, MAP_SHARED, fb_fd, 0);

	/*
	** Attempt filesystem mounts.  We use fork()/exec() so that we keep
	** going in case it stalls.  Also system() unavailable in this env.
	*/
	if (fork() == 0)  {	/* child? */
		char *arg[] = {"/sbin/sh", "-c",
			"mkdir /tmp/m_data /tmp/m_system; "
			"mount "MOUNT_PATH"/UDA /tmp/m_data; "
			"mount "MOUNT_PATH"/APP /tmp/m_system", NULL};
		execvp(arg[0], arg);
		exit(0);
	}

	sleep(4);
	if (argc >= 3)				/* cmdline menu selection */
		menu_sel = atoi(argv[2]);
	if (argc >= 2)				/* config file */
		read_config(argv[1], &menu_sel, &countdown);

	/*
	** Watch for events over the next few seconds.  Redraw upon click.
	*/
	while (countdown-- >= 1)  {
		for (x = 0;  x < 3;  x++)  {
			memset(&in_ev, 0, sizeof(in_ev));
			read(ev_fd[x], &in_ev, sizeof(in_ev));
			if ((in_ev.type  == EV_KEY)    &&
			    (in_ev.code  == KEY_POWER) &&
			    (in_ev.value == 1/*press*/))  {
				menu_sel++;
				countdown = 2/*sec*/ * 10;
			}
		}

		if (menu_sel != menu_old)  {
			fprintf(stderr, "need redraw\n");

			/* menu text */
			if (menu_sel > MAX_MENU_SEL)
				menu_sel = 1;

			memcpy(fb_tmp2_buf, fb_tmp1_buf, FB_SIZE);

			write_text(fb_tmp2_buf, xres, yres, fontmap,
				 6, xres / 200, FALSE,
				"Ouya Bootmenu");
			write_text(fb_tmp2_buf, xres, yres, fontmap,
				 7, xres / 200, FALSE,
				"-------------");

			write_text(fb_tmp2_buf, xres, yres, fontmap,
				 9, xres / 200, (menu_sel==1),
				"Normal Boot");
			write_text(fb_tmp2_buf, xres, yres, fontmap,
				 10, xres / 200, (menu_sel==2),
				"Alternate Boot");
			write_text(fb_tmp2_buf, xres, yres, fontmap,
				 11, xres / 200, (menu_sel==3),
				"Recovery");
			write_text(fb_tmp2_buf, xres, yres, fontmap,
				 13, xres / 200, (menu_sel==4),
				"Bootloader");
			write_text(fb_tmp2_buf, xres, yres, fontmap,
				 14, xres / 200, (menu_sel==5),
				"Failsafe");

			write_text(fb_tmp2_buf, xres, yres, fontmap,
				 19, xres / 200, FALSE,
				"POWER moves to next item");
			write_text(fb_tmp2_buf, xres, yres, fontmap,
				 20, xres / 200, FALSE,
				"Wait two seconds to select");

			if (fb_fi.smem_len >= 1)
				memcpy(fb_map_buf, fb_tmp2_buf, FB_SIZE);

			menu_old = menu_sel;
		}

		usleep(100000);		/* 1/10 second */
	}

	/*
	** Process current selection.
	*/
	switch (menu_sel)  {
		case 1:
			img_pname = "/tmp/m_system/boot.img";
			break;
		case 2:
			img_pname = "/tmp/m_data/media/altboot.img";
			img2_pname = "/tmp/m_data/media/0/altboot.img";
			break;
		case 3:
			fprintf(stderr, "reboot recovery\n");
			sync();
			if (fork() == 0)  {
				char *arg[] = {"/sbin/sh", "-c",
					"reboot recovery", NULL};
				execvp(arg[0], arg);
				exit(0);
			}

			sleep(2);
			break;
		case 4:
			fprintf(stderr, "reboot bootloader\n");
			sync();
			if (fork() == 0)  {
				char *arg[] = {"/sbin/sh", "-c",
					"reboot bootloader", NULL};
				execvp(arg[0], arg);
				exit(0);
			}

			sleep(2);
			break;
		case 5:
			fprintf(stderr, "failsafe exit (to CWM)\n");
			break;
	}

	/* Android image / KExec management */
	if ((strlen(img_pname) >= 1) &&
	    (extract_files(img_pname) || extract_files(img2_pname)))  {
		fprintf(stderr, "attempt kexec\n");
		sync();
		if (fork() == 0)  {
			char *arg[] = {"/sbin/sh", "-c",
				"kexec --load-hardboot /tmp/zImage"
				" --initrd /tmp/initramfs.cpio.gz"
				" --mem-min=0x8E000000"
				" --command-line=\"$(cat /tmp/cmdline"
						      " /proc/cmdline)\" ; "
				"kexec -e", NULL};
			execvp(arg[0], arg);
			exit(0);
		}

		sleep(2);
	}

	/*
	** Wrap up, assuming we reached this far.
	*/
	fprintf(stderr, "close and exit\n");
	if (fork() == 0)  {
		char *arg[] = {"/sbin/sh", "-c",
			"umount /tmp/m_data; umount /tmp/m_system", NULL};
		execvp(arg[0], arg);
		exit(0);
	}

	sleep(1);
	free(fontmap);
	if (fb_fi.smem_len >= 1)
		munmap(fb_map_buf, fb_fi.smem_len);
	close(fb_fd);
	close(ev_fd[0]); close(ev_fd[1]); close(ev_fd[2]);
	return 0;
}


/*
** Open/read configuration.  Includes default menu selection and timeout.
*/
int read_config(char *fname, int *menu, int *timeout)
{
	int				ret;
	int				v1 = -1, v2 = -1;
	FILE				*fp;
	char				pname[100];

	sprintf(pname, "/tmp/m_data/media/%s", fname);
	if ((fp = fopen(pname, "r")) != NULL)  {
		fprintf(stderr, "opened cfg\n");
		ret = fscanf(fp, "%d %d", &v1, &v2);
		if ((v1 >= 1) && (v1 <= MAX_MENU_SEL) &&
		    (v2 >= 0) && (v2 <= 30/*sec*/))  {
			fprintf(stderr, "update cfg\n");
			*menu    = v1;
			*timeout = v2 * 10;
		}

		fclose(fp);
	}

	return 0;
}


/*
** Write a set of characters to the supplied framebuffer memory region.
*/
int  write_text(char *fb_tmp_buf, int xres, int yres,
		unsigned char *fontmap,
		int row_num, int col_num, int reverse_vid, char *msg)
{
	int				c, x, y;
	int				map_idx;

	if (col_num < 0)	/* request centering */
		col_num = 0.4 + (xres - strlen(msg) * font.cwidth) /
				(2.0 * font.cwidth);

	for (c = 0;  c < strlen(msg);   c++)
	for (x = 0;  x < font.cwidth;   x++)
	for (y = 0;  y < font.cheight;  y++)  {
		if (((fontmap[(msg[c] - ' ') * font.cwidth +
			      y * font.width + x]) != 0) ^ reverse_vid)  {
			map_idx = BPP * ((col_num + c) * font.cwidth +
					 (row_num * font.cheight + y) *
					 xres + x);
			fb_tmp_buf[map_idx + 0/*red*/] = 0xff;
			fb_tmp_buf[map_idx + 1/*grn*/] = 0xff;
			fb_tmp_buf[map_idx + 2/*blu*/] = 0xff;
		}
	}

	return 0;
}


/*
** Extract the kernel and ramdisk components from the supplied image.
** Return TRUE if success.
*/
int extract_files(char *img_pname)
{
	int				ret;
	FILE				*fp;
	char				*cmdl_p;
	unsigned int			page_sz;
	unsigned int			kern_sz, ramd_sz, kern_of, ramd_of;
	static char			buf[MAX_IMG_SIZE];

	fprintf(stderr, "trying extract %s\n", img_pname);

	memset(buf, 0, sizeof(buf));
	fp = fopen(img_pname, "r");
	if (fp == NULL)  {
		fprintf(stderr, "failed to open kernel image\n");
		return FALSE;
	}

	ret = fread(buf, 1, MAX_IMG_SIZE, fp);
	fclose(fp);
	if (memcmp(buf, "ANDROID!", 8) != 0)  {
		fprintf(stderr, "not an 'ANDROID!' image\n");
		return FALSE;
	}

	kern_sz = *(int *)(buf + 0x08);
	ramd_sz = *(int *)(buf + 0x10);
	page_sz = *(int *)(buf + 0x24);
	cmdl_p  = buf + 0x40;
	if ((page_sz <  256)          || (page_sz >  65536) ||
	    (kern_sz >= MAX_IMG_SIZE) || (ramd_sz >= MAX_IMG_SIZE))  {
		fprintf(stderr, "corrupt kernel image (sizes)\n");
		return FALSE;
	}

	kern_of = page_sz;
	ramd_of = kern_of + ((kern_sz + page_sz - 1) / page_sz) * page_sz;
	if ((kern_of >= MAX_IMG_SIZE) || (ramd_of >= MAX_IMG_SIZE))  {
		fprintf(stderr, "corrupt kernel image (offsets)\n");
		return FALSE;
	}

	fp = fopen("/tmp/zImage", "w");
	fwrite(buf + kern_of, 1, kern_sz, fp);
	fclose(fp);

	fp = fopen("/tmp/initramfs.cpio.gz", "w");
	fwrite(buf + ramd_of, 1, ramd_sz, fp);
	fclose(fp);

	fp = fopen("/tmp/cmdline", "w");
	if (strlen(cmdl_p) >= 1)  {
		fwrite(cmdl_p, 1, strlen(cmdl_p), fp);
		fwrite(" ",    1, 1,              fp);
	}
	fclose(fp);

	return TRUE;
}
