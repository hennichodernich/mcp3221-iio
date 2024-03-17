// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mcp3221.c - driver for the Microchip mcp3221
 *
 * Copyright (C) 2013, Angelo Compagnucci, 2022 Henning Paul
 * Author:  Angelo Compagnucci <angelo.compagnucci@gmail.com>
 *          Henning Paul <hnch@gmx.net>
 *
 * This driver exports the value of analog input voltage to sysfs, the
 * voltage unit is nV.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <asm/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define MCP3221_CHAN(_index) \
	{ \
		.type = IIO_VOLTAGE, \
		.indexed = 1, \
		.channel = _index, \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) \
				| BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	}

/* Client data (each client gets its own) */
struct mcp3221 {
	struct i2c_client *i2c;
	u8 id;
	struct mutex lock;
};

static int mcp3221_read(struct mcp3221 *adc, int *value)
{
	int ret = 0;
	u8 buf[4] = {0, 0, 0, 0};
	u32 temp;

		ret = i2c_master_recv(adc->i2c, buf, 2);
		temp = get_unaligned_be16(&buf[0]);

	*value = sign_extend32(temp, 11);

	return ret;
}

static int mcp3221_read_channel(struct mcp3221 *adc,
				struct iio_chan_spec const *channel, int *value)
{
	int ret;
	u8 config;
	u8 req_channel = channel->channel;

	mutex_lock(&adc->lock);
	ret = mcp3221_read(adc, value);
	mutex_unlock(&adc->lock);

	return ret;
}

static int mcp3221_read_raw(struct iio_dev *iio,
			struct iio_chan_spec const *channel, int *val1,
			int *val2, long mask)
{
	struct mcp3221 *adc = iio_priv(iio);
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = mcp3221_read_channel(adc, channel, val1);
		if (err < 0)
			return -EINVAL;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:

		*val1 = 0;
		*val2 = 3300000000/4096; //nV per LSB
		return IIO_VAL_INT_PLUS_NANO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val1 = 5500;
		return IIO_VAL_INT;

	default:
		break;
	}

	return -EINVAL;
}

static int mcp3221_write_raw(struct iio_dev *iio,
			struct iio_chan_spec const *channel, int val1,
			int val2, long mask)
{
	struct mcp3221 *adc = iio_priv(iio);
	u8 temp;
	u8 i;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return -EINVAL;

	case IIO_CHAN_INFO_SAMP_FREQ:
		return -EINVAL;

	default:
		break;
	}

	return -EINVAL;
}

static int mcp3221_write_raw_get_fmt(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static ssize_t mcp3221_show_samp_freqs(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mcp3221 *adc = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "5500\n");
}

static ssize_t mcp3221_show_scales(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mcp3221 *adc = iio_priv(dev_to_iio_dev(dev));

	return sprintf(buf, "0.%09u\n",	3300000000/4096);
}

static IIO_DEVICE_ATTR(sampling_frequency_available, S_IRUGO,
		mcp3221_show_samp_freqs, NULL, 0);
static IIO_DEVICE_ATTR(in_voltage_scale_available, S_IRUGO,
		mcp3221_show_scales, NULL, 0);

static struct attribute *mcp3221_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group mcp3221_attribute_group = {
	.attrs = mcp3221_attributes,
};

static const struct iio_chan_spec mcp3221_channels[] = {
	MCP3221_CHAN(0),
};

static const struct iio_info mcp3221_info = {
	.read_raw = mcp3221_read_raw,
	.write_raw = mcp3221_write_raw,
	.write_raw_get_fmt = mcp3221_write_raw_get_fmt,
	.attrs = &mcp3221_attribute_group,
};

static int mcp3221_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct mcp3221 *adc;
	int err;
	u8 config;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->i2c = client;

	mutex_init(&adc->lock);

	indio_dev->name = client->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mcp3221_info;

	indio_dev->channels = mcp3221_channels;
	indio_dev->num_channels = 1;

	err = devm_iio_device_register(&client->dev, indio_dev);
	if (err < 0)
		return err;

	i2c_set_clientdata(client, indio_dev);

	return 0;
}

static const struct i2c_device_id mcp3221_id[] = {
	{ "mcp3221", 1 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcp3221_id);

static const struct of_device_id mcp3221_of_match[] = {
	{ .compatible = "mcp3221" },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp3221_of_match);

static struct i2c_driver mcp3221_driver = {
	.driver = {
		.name = "mcp3221",
		.of_match_table = mcp3221_of_match,
	},
	.probe = mcp3221_probe,
	.id_table = mcp3221_id,
};
module_i2c_driver(mcp3221_driver);

MODULE_AUTHOR("Henning Paul <hnch@gmx.net>");
MODULE_DESCRIPTION("Microchip MCP3221 driver");
MODULE_LICENSE("GPL v2");
