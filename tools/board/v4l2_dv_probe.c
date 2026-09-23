#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *device = argc > 1 ? argv[1] : "/dev/v4l-subdev0";
	const char *video_device = argc > 2 ? argv[2] : NULL;
	struct v4l2_dv_timings timings;
	const struct v4l2_bt_timings *bt;
	uint64_t total_width;
	uint64_t total_height;
	double fps;
	int video_fd = -1;
	int fd;

	/*
	 * Allwinner's VIN driver powers and initializes the HDMI receiver on
	 * VIDIOC_S_INPUT. It does not call s_stream until VIDIOC_STREAMON.
	 */
	if (video_device) {
		unsigned int input = 0;

		video_fd = open(video_device, O_RDWR | O_CLOEXEC);
		if (video_fd < 0) {
			fprintf(stderr, "open %s: %s\n", video_device,
				strerror(errno));
			return 1;
		}
		if (ioctl(video_fd, VIDIOC_S_INPUT, &input) < 0) {
			fprintf(stderr, "VIDIOC_S_INPUT: %s\n", strerror(errno));
			close(video_fd);
			return 1;
		}
	}

	fd = open(device, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", device, strerror(errno));
		if (video_fd >= 0)
			close(video_fd);
		return 1;
	}

	memset(&timings, 0, sizeof(timings));
	if (ioctl(fd, VIDIOC_SUBDEV_QUERY_DV_TIMINGS, &timings) < 0) {
		fprintf(stderr, "VIDIOC_SUBDEV_QUERY_DV_TIMINGS: %s\n",
			strerror(errno));
		close(fd);
		if (video_fd >= 0)
			close(video_fd);
		return 1;
	}
	close(fd);
	if (video_fd >= 0)
		close(video_fd);

	if (timings.type != V4L2_DV_BT_656_1120) {
		fprintf(stderr, "unexpected timings type: %u\n", timings.type);
		return 1;
	}

	bt = &timings.bt;
	total_width = bt->width + bt->hfrontporch + bt->hsync +
		      bt->hbackporch;
	total_height = bt->height + bt->vfrontporch + bt->vsync +
		       bt->vbackporch + bt->il_vfrontporch +
		       bt->il_vsync + bt->il_vbackporch;
	fps = total_width && total_height
		      ? (double)bt->pixelclock /
				(double)(total_width * total_height)
		      : 0.0;

	printf("active=%ux%u total=%" PRIu64 "x%" PRIu64
	       " pixelclock=%" PRIu64 " fps=%.6f interlaced=%u\n",
	       bt->width, bt->height, total_width, total_height,
	       (uint64_t)bt->pixelclock, fps, bt->interlaced);
	printf("hfp=%u hsync=%u hbp=%u vfp=%u vsync=%u vbp=%u\n",
	       bt->hfrontporch, bt->hsync, bt->hbackporch,
	       bt->vfrontporch, bt->vsync, bt->vbackporch);
	return 0;
}
