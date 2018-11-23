/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Dicky Chiang <dicky_chiang@ilitek.com>
 * Based on TDD v7.0 implemented by Mstar & ILITEK
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "../common.h"
#include "../platform.h"
#include "config.h"
#include "spi.h"
#include "finger_report.h"
#include "mp_test.h"

//#include "mt_spi_hal.h"
#include "mt_spi.h"
#include "sync_write.h"

struct core_spi_data *core_spi;

#define DMA_TRANSFER_MAX_TIMES 2
#define DMA_TRANSFER_MAX_SIZE 1024
#define SPI_WRITE_BUFF_MAXSIZE (1024 * DMA_TRANSFER_MAX_TIMES + 5)//plus 5 for IC Mode :(Head + Address) 0x82,0x25,Addr_L,Addr_M,Addr_H
#define SPI_READ_BUFF_MAXSIZE  (1024 * DMA_TRANSFER_MAX_TIMES)
__attribute__ ((section ("NONCACHEDRW"), aligned(4))) 
uint8_t dma_txbuf[SPI_WRITE_BUFF_MAXSIZE] = {0};
__attribute__ ((section ("NONCACHEDRW"), aligned(4))) 
uint8_t dma_rxbuf[SPI_READ_BUFF_MAXSIZE] = {0};
// Note: take care for IRAM 4byte address alignment
int core_spi_write_then_read(struct spi_device *spi,
		const uint8_t *txbuf, unsigned n_tx,
		uint8_t *rxbuf, unsigned n_rx)
{
	static DEFINE_MUTEX(lock);
	int	status = -1; 
	struct spi_message	message;
	struct spi_transfer	xfer[DMA_TRANSFER_MAX_TIMES+1];
	uint8_t temp1[1] = { 0 }, temp2[1] = {0};
	int xfercnt = 0, xferlen = 0, xferloop = 0;

	if(n_tx > (SPI_WRITE_BUFF_MAXSIZE)){
		ipio_err("[spi write] Exceeded transmission length, %d > Max length:%d\n", n_tx,SPI_WRITE_BUFF_MAXSIZE);
		goto out;
	}
	if(n_rx > (SPI_READ_BUFF_MAXSIZE)){
		ipio_err("[spi read] Exceeded transmission length, %d > Max length:%d\n", n_rx,SPI_READ_BUFF_MAXSIZE);
		goto out;
	}

	mutex_trylock(&lock);
	if(txbuf[0] == 0x82){ // for SPI Write via DMA
		spi_message_init(&message);
		memset(xfer, 0, sizeof(xfer));

		if(n_tx % DMA_TRANSFER_MAX_SIZE){
			xferloop = (n_tx/DMA_TRANSFER_MAX_SIZE) + 1;
		}else{
			xferloop = n_tx / DMA_TRANSFER_MAX_SIZE ;
		}
		xferlen = n_tx;
		memcpy(dma_txbuf, txbuf,xferlen);
		for(xfercnt=0; xfercnt<xferloop; xfercnt++){
			if(xferlen > DMA_TRANSFER_MAX_SIZE){
				xferlen = DMA_TRANSFER_MAX_SIZE;
			}
			xfer[xfercnt].len = xferlen;
			xfer[xfercnt].tx_buf = dma_txbuf + xfercnt * DMA_TRANSFER_MAX_SIZE;
			spi_message_add_tail(&xfer[xfercnt], &message);

			xferlen = n_tx - (xfercnt+1) * DMA_TRANSFER_MAX_SIZE;
		}
		status = spi_sync(spi, &message);
	}
	else{// for SPI Read via DMA
		spi_message_init(&message);
		memset(xfer, 0, sizeof(xfer));
		//---for write cmd and head
		memcpy(dma_txbuf, txbuf,n_tx);
		xfer[0].len = n_tx;
		xfer[0].tx_buf = dma_txbuf;
		xfer[0].rx_buf = temp1;
		spi_message_add_tail(&xfer[0], &message);
		//---for read data
		if(n_rx % DMA_TRANSFER_MAX_SIZE){
			xferloop = (n_rx / DMA_TRANSFER_MAX_SIZE) + 1;
		}else{
			xferloop = n_rx / DMA_TRANSFER_MAX_SIZE ;
		}
		xferlen = n_rx;
		for(xfercnt=0; xfercnt<xferloop; xfercnt++){
			if(xferlen > DMA_TRANSFER_MAX_SIZE){
				xferlen = DMA_TRANSFER_MAX_SIZE;
			}
			xfer[xfercnt+1].len = xferlen;
			xfer[xfercnt+1].tx_buf = temp2;
			xfer[xfercnt+1].rx_buf = dma_rxbuf + xfercnt * DMA_TRANSFER_MAX_SIZE;
			spi_message_add_tail(&xfer[xfercnt+1], &message);

			xferlen = n_rx - (xfercnt+1) * DMA_TRANSFER_MAX_SIZE;
		}
		status = spi_sync(spi, &message);
		if(status == 0){
			memcpy(rxbuf, dma_rxbuf, n_rx);
		}
	}
	mutex_unlock(&lock);
out:
	return status;
}
EXPORT_SYMBOL_GPL(core_spi_write_then_read);

int core_Rx_check(uint16_t check)
{
	int size = 0, i, count = 100;
	uint8_t txbuf[5] = { 0 }, rxbuf[4] = {0};
	uint16_t status = 0;

	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x94;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;

	for (i = 0; i < count; i++) {

		txbuf[0] = SPI_WRITE;
		if (core_spi_write_then_read(core_spi->spi, txbuf, 5, txbuf, 0) < 0) {
			size = -EIO;
			ipio_err("spi Write Error, ret = %d\n", size);
		}

		txbuf[0] = SPI_READ;
		if (core_spi_write_then_read(core_spi->spi, txbuf, 1, rxbuf, 4) < 0) {
			size = -EIO;
			ipio_err("spi Read Error, ret = %d\n", size);
		}

		status = (rxbuf[2] << 8) + rxbuf[3];
		size = (rxbuf[0] << 8) + rxbuf[1];

		if (status == check)
			return size;

		mdelay(1);
	}

	size = -EIO;
	ipio_err("Check lock error\n");
	return size;
}

int core_Tx_unlock_check(void)
{
	int ret = 0, i, count = 100;
	uint8_t txbuf[5] = { 0 }, rxbuf[4] = {0};
	uint16_t unlock = 0;

	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x0;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;

	for (i = 0; i < count; i++) {
		txbuf[0] = SPI_WRITE;
		if (core_spi_write_then_read(core_spi->spi, txbuf, 5, txbuf, 0) < 0) {
			ret = -EIO;
			ipio_err("spi Write Error, ret = %d\n", ret);
		}

		txbuf[0] = SPI_READ;
		if (core_spi_write_then_read(core_spi->spi, txbuf, 1, rxbuf, 4) < 0) {
			ret = -EIO;
			ipio_err("spi Read Error, ret = %d\n", ret);
		}

		unlock = (rxbuf[2] << 8) + rxbuf[3];

		if (unlock == 0x9881)
			return ret;

		mdelay(1);
	}

	ipio_err("Check unlock error\n");
	return ret;
}

int core_spi_ice_mode_unlock_read(uint8_t *data, uint32_t size)
{
	int ret = 0;
	uint8_t txbuf[64] = { 0 };

	/* set read address */
	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x98;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;
	if (core_spi_write_then_read(core_spi->spi, txbuf, 5, txbuf, 0) < 0) {
		ret = -EIO;
		return ret;
	}

	/* read data */
	txbuf[0] = SPI_READ;
	if (core_spi_write_then_read(core_spi->spi, txbuf, 1, data, size) < 0) {
		ret = -EIO;
		return ret;
	}

	/* write data unlock */
	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x94;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;
	txbuf[5] = (size & 0xFF00) >> 8;
	txbuf[6] = size & 0xFF;
	txbuf[7] = (char)0x98;
	txbuf[8] = (char)0x81;
	if (core_spi_write_then_read(core_spi->spi, txbuf, 9, txbuf, 0) < 0) {
		ret = -EIO;
		ipio_err("spi Write data unlock error, ret = %d\n", ret);
	}

	return ret;
}

int core_spi_ice_mode_lock_write(uint8_t *data, uint32_t size)
{
	int ret = 0;
	int safe_size = size;
	uint8_t check_sum = 0,wsize = 0;
	uint8_t *txbuf = NULL;

    txbuf = kcalloc(size + 9, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(txbuf)) {
		ipio_err("Failed to allocate txbuf\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Write data */
	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x4;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;

	/* Calcuate checsum and fill it in the last byte */
	check_sum = core_fr_calc_checksum(data, size);
	ipio_memcpy(txbuf + 5, data, size, safe_size + 9);
	txbuf[5 + size] = check_sum;
	size++;
	wsize = size;
	if (wsize % 4 != 0)
		wsize += 4 - (wsize % 4);

	if (core_spi_write_then_read(core_spi->spi, txbuf, wsize + 5, txbuf, 0) < 0) {
		ret = -EIO;
		ipio_err("spi Write Error, ret = %d\n", ret);
		goto out;
	}

	/* write data lock */
	txbuf[0] = SPI_WRITE;
	txbuf[1] = 0x25;
	txbuf[2] = 0x0;
	txbuf[3] = 0x0;
	txbuf[4] = 0x2;
	txbuf[5] = (size & 0xFF00) >> 8;
	txbuf[6] = size & 0xFF;
	txbuf[7] = (char)0x5A;
	txbuf[8] = (char)0xA5;
	if (core_spi_write_then_read(core_spi->spi, txbuf, 9, txbuf, 0) < 0) {
		ret = -EIO;
		ipio_err("spi Write data lock Error, ret = %d\n", ret);
	}

out:
	ipio_kfree((void **)&txbuf);
	return ret;
}

int core_spi_ice_mode_disable(void)
{
	int ret = 0;
	uint8_t txbuf[5] = {0};

	txbuf[0] = 0x82;
	txbuf[1] = 0x1B;
	txbuf[2] = 0x62;
	txbuf[3] = 0x10;
	txbuf[4] = 0x18;

	if (core_spi_write_then_read(core_spi->spi, txbuf, 5, txbuf, 0) < 0) {
		ret = -EIO;
		ipio_err(" Error, ret = %d\n", ret);
	}

	return ret;
}

int core_spi_ice_mode_enable(void)
{
	int ret = 0;
	uint8_t txbuf[5] = {0}, rxbuf[2]= {0};

	txbuf[0] = 0x82;
	txbuf[1] = 0x1F;
	txbuf[2] = 0x62;
	txbuf[3] = 0x10;
	txbuf[4] = 0x18;

	// if (core_spi_write_then_read(core_spi->spi, txbuf, 1, rxbuf, 1) < 0) {
	// 	ret = -EIO;
	// 	ipio_err("spi Write Error, ret = %d\n", ret);
	// 	return ret;
	// }

	// /* check recover data */
	// if (rxbuf[0] == 0x82) {
	// 	ipio_err("Check Recovery data failed (0x%x)\n", rxbuf[0]);
	// 	return CHECK_RECOVER;
	// }

	if (core_spi_write_then_read(core_spi->spi, txbuf, 5, rxbuf, 0) < 0) {
		ret = -EIO;
		ipio_err("spi Write Error, ret = %d\n", ret);
	}

	return ret;
}

int core_spi_ice_mode_read(uint8_t *pBuf, uint16_t nSize)
{
	int ret = 0, size = 0;

	ret = core_spi_ice_mode_enable();
	if (ret < 0) {
		goto out;
	}

	size = core_Rx_check(0x5AA5);
	if (size < 0) {
		ret = -EIO;
		goto out;
	}

	if (core_spi_ice_mode_unlock_read(pBuf, size) < 0) {
		ret = -EIO;
		goto out;
	}

	if (core_spi_ice_mode_disable() < 0) {
		ret = -EIO;
		goto out;
	}

out:
	return ret;
}

int core_spi_ice_mode_write(uint8_t *pBuf, uint16_t nSize)
{
	int ret = 0;
	uint8_t *txbuf = NULL;

    txbuf = kcalloc(nSize + 5, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(txbuf)) {
		ipio_err("Failed to allocate txbuf\n");
		return -ENOMEM;
	}

	ret = core_spi_ice_mode_enable();
	if (ret < 0)
		goto out;

	ret = core_spi_ice_mode_lock_write(pBuf, nSize);
	if (ret < 0)
		goto out;

	if (core_Tx_unlock_check() < 0) {
		ret = -ETXTBSY;
	}

out:
	ipio_kfree((void **)&txbuf);
	return ret;
}

int core_spi_write(uint8_t *pBuf, uint16_t nSize)
{
	int ret = 0;
	uint8_t *txbuf = NULL;
	uint16_t safe_size = nSize;

    txbuf = kcalloc(nSize + 1, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(txbuf)) {
		ipio_err("Failed to allocate txbuf\n");
		return -ENOMEM;
		goto out;
	}

	if (core_config->icemodeenable == false) {
		ret = core_spi_ice_mode_write(pBuf, nSize);
		core_spi_ice_mode_disable();
		ipio_kfree((void **)&txbuf);
		return ret;
	}

	txbuf[0] = SPI_WRITE;
	ipio_memcpy(txbuf+1, pBuf, nSize, safe_size + 1);

	if (core_spi_write_then_read(core_spi->spi, txbuf, nSize+1, txbuf, 0) < 0) {
		if (atomic_read(&ipd->do_reset)) {
			/* ignore spi error if doing ic reset */
			ret = 0;
		} else {
			ret = -EIO;
			ipio_err("spi Write Error, ret = %d\n", ret);
			goto out;
		}
	}

out:
	ipio_kfree((void **)&txbuf);
	return ret;
}
EXPORT_SYMBOL(core_spi_write);

int core_spi_read(uint8_t *pBuf, uint16_t nSize)
{
	int ret = 0;
	uint8_t txbuf[1] = {0};

	txbuf[0] = SPI_READ;

	if (core_config->icemodeenable == false)
		return core_spi_ice_mode_read(pBuf, nSize);

	if(core_spi_write_then_read(core_spi->spi, txbuf, 1, pBuf, nSize) < 0) {
		if (atomic_read(&ipd->do_reset)) {
			/* ignore spi error if doing ic reset */
			ret = 0;
		} else {
			ret = -EIO;
			ipio_err("spi Read Error, ret = %d\n", ret);
			goto out;
		}
	}
	//dump_data(pBuf,8,nSize,0,"spi_write_then_read");
out:
	return ret;
}
EXPORT_SYMBOL(core_spi_read);

int core_spi_setup(struct spi_device *spi, uint32_t frequency)
{
	int ret;
	struct mt_chip_conf *chip_config;
	uint32_t temp_pulsewidth = 0;
	
	chip_config = (struct mt_chip_conf *)spi->controller_data;
	if (!chip_config) {
		ipio_err("chip_config is NULL.\n");
		chip_config = kzalloc(sizeof(struct mt_chip_conf), GFP_KERNEL);
		if (!chip_config)
			return -ENOMEM;
	}

	temp_pulsewidth = ((112 * 1000000) / frequency);
	temp_pulsewidth = temp_pulsewidth / 2; 

	//if()

	chip_config->setuptime = temp_pulsewidth * 2;// for CS ,  temp_pulsewidth*9.6ns= 
	chip_config->holdtime = temp_pulsewidth * 2;// for CS
	chip_config->high_time = temp_pulsewidth;// for CLK = 1M
	chip_config->low_time = temp_pulsewidth;// for CLK= 1M
	chip_config->cs_idletime = temp_pulsewidth * 2;// for CS
	chip_config->rx_mlsb = 1;
	chip_config->tx_mlsb = 1;
	chip_config->tx_endian = 0;
	chip_config->rx_endian = 0;
	chip_config->cpol = 0;
	chip_config->cpha = 0;
	chip_config->com_mod = DMA_TRANSFER;
	//chip_config->com_mod = FIFO_TRANSFER;
	chip_config->pause = 1;
	chip_config->finish_intr = 1;
	chip_config->deassert = 0;

	spi->controller_data = chip_config;
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;

	ret = spi_setup(spi);
	if (ret < 0) {
		ipio_err("ERR: fail to setup spi\n");
		return -ENODEV;
	}

	ipio_info("name = %s, bus_num = %d,cs = %d, mode = %d, speed = %d\n",spi->modalias,
	 spi->master->bus_num, spi->chip_select, spi->mode, spi->max_speed_hz);

	return 0;
}
EXPORT_SYMBOL(core_spi_setup);

void core_spi_speed_up(struct spi_device *spi, uint32_t chip_id)
{
	if (!spi)
		return;

	//if (spi->max_speed_hz > 8600000) {
		if (chip_id == CHIP_TYPE_ILI7807) {
			ipio_info("Set register for SPI seepd up \n");
			core_config_ice_mode_enable();
			core_config_ice_mode_write(0x063820, 0x00000101, 4);
			core_config_ice_mode_write(0x042c34, 0x00000008, 4);
			core_config_ice_mode_write(0x063820, 0x00000000, 4);
			core_config_ice_mode_disable();
		}
	//}

	// if (core_spi_setup(spi, 10000000) < 0) {
	// 	ipio_err("ERR: fail to setup spi\n");
	// 	//return -ENODEV;
	// }
}
EXPORT_SYMBOL(core_spi_speed_up);

// int core_spi_init(struct spi_device *spi)
// {
// 	int ret;

// 	core_spi = devm_kmalloc(ipd->dev, sizeof(struct core_spi_data), GFP_KERNEL);
// 	if (ERR_ALLOC_MEM(core_spi)) {
// 		ipio_err("Failed to alllocate core_i2c mem %ld\n", PTR_ERR(core_spi));
// 		return -ENOMEM;
// 	}

// 	spi->mode = SPI_MODE_0;
// 	spi->bits_per_word = 8;
// 	//spi->max_speed_hz = 1000000;
// 	//spi->master->max_speed_hz = 1000000;

// 	ret = spi_setup(spi);
// 	if (ret < 0) {
// 		ipio_err("ERR: fail to setup spi\n");
// 		return -ENODEV;
// 	}

// 	ipio_info("mode_bits : %d",spi->master->mode_bits);
// 	ipio_info("max_speed_hz : %d",spi->master->max_speed_hz);

// 	//ipio_info("name = %s, bus_num = %d,cs = %d, mode = %d, speed = %d\n",spi->modalias,
// 	// spi->master->bus_num, spi->chip_select, spi->mode, spi->max_speed_hz);

// 	core_spi->spi = spi;
// 	return 0;
// }
// EXPORT_SYMBOL(core_spi_init);


int core_spi_init(struct spi_device *spi)
{
	int ret;

	//struct mt_chip_conf *chip_config;
	//struct spi_master *master;
	//struct mt_spi_t *ms;
	//master = spi->master;
	//ms = spi_master_get_devdata(master);

	core_spi = devm_kmalloc(ipd->dev, sizeof(struct core_spi_data), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_spi)) {
		ipio_err("Failed to alllocate core_i2c mem %ld\n", PTR_ERR(core_spi));
		return -ENOMEM;
	}

	ret = core_spi_setup(spi, 1000000);
	if (ret < 0) {
		ipio_err("ERR: fail to setup spi\n");
		return -ENODEV;
	}

	ipio_info("name = %s, bus_num = %d,cs = %d, mode = %d, speed = %d\n",spi->modalias,
	 spi->master->bus_num, spi->chip_select, spi->mode, spi->max_speed_hz);

	core_spi->spi = spi;
	return 0;
}
EXPORT_SYMBOL(core_spi_init);