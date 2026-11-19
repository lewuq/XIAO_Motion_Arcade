#include "hardware.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define TOUCH_NODE DT_NODELABEL(round_touch)
#define TOUCH_BUS DT_BUS(TOUCH_NODE)

#ifndef MAZE_TOUCH_D0_PULSE
#define MAZE_TOUCH_D0_PULSE 1
#endif

#ifndef MAZE_TOUCH_SWAP
#define MAZE_TOUCH_SWAP 0
#endif
#ifndef MAZE_TOUCH_X_SIGN
#define MAZE_TOUCH_X_SIGN 1
#endif
#ifndef MAZE_TOUCH_Y_SIGN
#define MAZE_TOUCH_Y_SIGN 1
#endif

static const struct device *const panel = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const imu = DEVICE_DT_GET(DT_ALIAS(imu0));
static const struct i2c_dt_spec touch_bus = I2C_DT_SPEC_GET(TOUCH_NODE);
static const struct gpio_dt_spec backlight = GPIO_DT_SPEC_GET(DT_NODELABEL(round_backlight), gpios);
static const struct gpio_dt_spec sd_select = GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi3), cs_gpios, 1);
static const struct gpio_dt_spec touch_irq = GPIO_DT_SPEC_GET(TOUCH_NODE, irq_gpios);
static const struct gpio_dt_spec touch_sda = GPIO_DT_SPEC_GET(TOUCH_BUS, sda_gpios);
static const struct gpio_dt_spec touch_scl = GPIO_DT_SPEC_GET(TOUCH_BUS, scl_gpios);

#if MAZE_TOUCH_D0_PULSE
static const struct gpio_dt_spec touch_d0 = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioa)),
	.pin = 0,
};
#endif

static bool transport_confirmed;
static int64_t touch_log_after;
static unsigned int touch_errors;

int panel_init(void)
{
	if (!device_is_ready(panel) || !gpio_is_ready_dt(&backlight) ||
	    !gpio_is_ready_dt(&sd_select)) {
		return -ENODEV;
	}
	int ret = gpio_pin_configure_dt(&sd_select, GPIO_OUTPUT_INACTIVE);

	if (ret == 0) {
		ret = gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_INACTIVE);
	}
	if (ret == 0) {
		ret = display_set_pixel_format(panel, PIXEL_FORMAT_RGB_565);
	}
	return ret;
}

int panel_show(void)
{
	int ret = display_blanking_off(panel);

	return ret == 0 ? gpio_pin_set_dt(&backlight, 1) : ret;
}

static unsigned int backlight_on_us;

static void backlight_off(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	gpio_pin_set_dt(&backlight, 0);
}

K_TIMER_DEFINE(backlight_fall, backlight_off, NULL);

static void backlight_on(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	gpio_pin_set_dt(&backlight, 1);
	k_timer_start(&backlight_fall, K_USEC(backlight_on_us), K_NO_WAIT);
}

K_TIMER_DEFINE(backlight_cycle, backlight_on, NULL);

void panel_brightness(unsigned int level)
{
	unsigned int key = irq_lock();

	k_timer_stop(&backlight_cycle);
	k_timer_stop(&backlight_fall);
	gpio_pin_set_dt(&backlight, 1);
	if (level == 1 || level == 2) {
		backlight_on_us = level == 1 ? 3500 : 2000;
		k_timer_start(&backlight_fall, K_USEC(backlight_on_us), K_NO_WAIT);
		k_timer_start(&backlight_cycle, K_MSEC(5), K_MSEC(5));
	}
	irq_unlock(key);
}

int panel_write(int x, int y, int width, int height, const uint16_t *pixels)
{
	const struct display_buffer_descriptor descriptor = {
		.width = width, .height = height, .pitch = width,
		.buf_size = (size_t)width * height * sizeof(*pixels),
	};

	return display_write(panel, x, y, &descriptor, pixels);
}

int imu_read(float accel[3])
{
	struct sensor_value sample[3];

	if (!device_is_ready(imu)) {
		return -ENODEV;
	}
	int ret = sensor_sample_fetch(imu);

	if (ret == 0) {
		ret = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, sample);
	}
	if (ret == 0) {
		for (unsigned int axis = 0; axis < 3; axis++) {
			accel[axis] = (sample[axis].val1 + sample[axis].val2 * 1.0e-6f) / 9.80665f;
		}
	}
	return ret;
}

int touch_prepare(void)
{
#if MAZE_TOUCH_D0_PULSE
	if (!gpio_is_ready_dt(&touch_d0)) {
		return -ENODEV;
	}
	int ret = gpio_pin_configure_dt(&touch_d0, GPIO_OUTPUT_HIGH);

	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_set_dt(&touch_d0, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(20);
	ret = gpio_pin_set_dt(&touch_d0, 1);
	if (ret < 0) {
		return ret;
	}
	k_msleep(30);
	printk("CHSC6X can_logger startup: D0 pulse complete\n");
#endif
	return 0;
}

int touch_init(void)
{
	if (!i2c_is_ready_dt(&touch_bus) || !gpio_is_ready_dt(&touch_irq)) {
		return -ENODEV;
	}
	k_msleep(100);
	int ret = gpio_pin_configure_dt(&touch_irq, GPIO_INPUT);

	if (ret == 0) {
		ret = i2c_configure(touch_bus.bus,
				    I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_STANDARD));
	}
	if (ret == 0) {
		printk("CHSC6X HW-I2C1 ready: 100k addr=0x%02x SDA=%d SCL=%d INT=%d (raw)\n",
		       touch_bus.addr, gpio_pin_get_raw(touch_sda.port, touch_sda.pin),
		       gpio_pin_get_raw(touch_scl.port, touch_scl.pin),
		       gpio_pin_get_raw(touch_irq.port, touch_irq.pin));
	}
	return ret;
}

static void touch_note_failure(int error)
{
	int64_t now = k_uptime_get();

	touch_errors++;
	if (now < touch_log_after) {
		return;
	}
	touch_log_after = now + 2000;
	int recovery = touch_errors == 1 ? -EAGAIN : i2c_recover_bus(touch_bus.bus);

	printk("CHSC6X HW-I2C1 read failed: %d; count=%u SDA=%d SCL=%d INT=%d (raw)",
	       error, touch_errors, gpio_pin_get_raw(touch_sda.port, touch_sda.pin),
	       gpio_pin_get_raw(touch_scl.port, touch_scl.pin),
	       gpio_pin_get_raw(touch_irq.port, touch_irq.pin));
	if (touch_errors != 1) {
		printk(" recover=%d", recovery);
	}
	printk("\n");
}

int touch_read(struct touch *point)
{
	uint8_t report[5];
	int active = gpio_pin_get_dt(&touch_irq);

	if (active <= 0) {
		return active;
	}
	int ret = i2c_read_dt(&touch_bus, report, sizeof(report));

	if (ret < 0) {
		touch_note_failure(ret);
		return ret;
	}
	touch_errors = 0;
	if (report[0] != 1) {
		return 0;
	}
	if (report[2] >= MAZE_WIDTH || report[4] >= MAZE_HEIGHT) {
		return -EAGAIN;
	}
	if (!transport_confirmed) {
		printk("CHSC6X HW-I2C1 touch OK: x=%u y=%u\n", report[2], report[4]);
		transport_confirmed = true;
	}
	int raw_x = MAZE_TOUCH_SWAP ? report[4] : report[2];
	int raw_y = MAZE_TOUCH_SWAP ? report[2] : report[4];

	point->x = MAZE_TOUCH_X_SIGN > 0 ? raw_x : MAZE_WIDTH - 1 - raw_x;
	point->y = MAZE_TOUCH_Y_SIGN > 0 ? raw_y : MAZE_HEIGHT - 1 - raw_y;
	return 1;
}
