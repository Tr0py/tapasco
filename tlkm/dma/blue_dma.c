//
// Copyright (C) 2017 Jaco A. Hofmann, TU Darmstadt
//
// This file is part of Tapasco (TPC).
//
// Tapasco is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Tapasco is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Tapasco.  If not, see <http://www.gnu.org/licenses/>.
//
#include <linux/sched.h>
#include "tlkm_dma.h"
#include "tlkm_logging.h"

/* Register Map and commands */
#define REG_HOST_ADDR 			0x00 	/* slv_reg0 = PCIe addr */
#define REG_FPGA_ADDR 			0x08	/* slv_reg1 = FPGA addr */
#define REG_BTT 			0x10	/* slv_reg2 = bytes to transfer */
#define REG_CMD 			0x20	/* slv_reg3 = CMD */

#define CMD_READ			0x10001000 	/* from m64 fpga memory to m64 host memory */
#define CMD_WRITE			0x10000001 	/* from m64 host memory to m64 fpga memory */

irqreturn_t blue_dma_intr_handler_read(int irq, void * dev_id)
{
	struct dma_engine *dma = (struct dma_engine *)dev_id;
	BUG_ON(dma->irq_no != irq);
	atomic64_inc(&dma->rq_processed);
	wake_up_interruptible_sync(&dma->rq);
	return IRQ_HANDLED;
}

irqreturn_t blue_dma_intr_handler_write(int irq, void * dev_id)
{
	struct dma_engine *dma = (struct dma_engine *)dev_id;
	BUG_ON(dma->irq_no != irq);
	atomic64_inc(&dma->wq_processed);
	wake_up_interruptible_sync(&dma->wq);
	return IRQ_HANDLED;
}

ssize_t blue_dma_copy_from(struct dma_engine *dma, void __user *usr_addr, dev_addr_t dev_addr, size_t len)
{
	LOG(TLKM_LF_DMA, "dev_addr = 0x%08llx, usr_addr = 0x%08llx, len: %zu bytes", (u64)dev_addr, (u64)usr_addr, len);
	if(mutex_lock_interruptible(&dma->regs_mutex)) {
		WRN("got killed while aquiring the mutex");
		return len;
	}

	*(u64 *)(dma->regs + REG_FPGA_ADDR)		= dev_addr;
	*(u64 *)(dma->regs + REG_HOST_ADDR)		= (u64)usr_addr;
	*(u64 *)(dma->regs + REG_BTT)			= len;
	wmb();
	*(u64 *)(dma->regs + REG_CMD)			= CMD_READ;
	mutex_unlock(&dma->regs_mutex);
	return atomic64_read(&dma->rq_processed) + 1;
}

ssize_t blue_dma_copy_to(struct dma_engine *dma, dev_addr_t dev_addr, const void __user *usr_addr, size_t len)
{
	LOG(TLKM_LF_DMA, "dev_addr = 0x%08llx, usr_addr = 0x%08llx, len: %zu bytes", (u64)dev_addr, (u64)usr_addr, len);
	if(mutex_lock_interruptible(&dma->regs_mutex)) {
		WRN("got killed while aquiring the mutex");
		return len;
	}

	*(u64 *)(dma->regs + REG_FPGA_ADDR)		= dev_addr;
	*(u64 *)(dma->regs + REG_HOST_ADDR)		= (u64)usr_addr;
	*(u64 *)(dma->regs + REG_BTT)			= len;
	wmb();
	*(u64 *)(dma->regs + REG_CMD)			= CMD_WRITE;
	mutex_unlock(&dma->regs_mutex);
	return atomic64_read(&dma->wq_processed) + 1;
}