/******************************************************************************
* (c) COPYRIGHT 2013 NEXELL, Inc. ALL RIGHTS RESERVED.
*
* This software is the property of NEXELL and is furnished under license
* by NEXELL.
* This software may be used only in accordance with the terms of said license.
* This copyright notice may not be removed, modified or obliterated without
* the prior written permission of NEXELL, Inc.
*
* This software may not be copied, transmitted, provided to or otherwise
* made available to any other person, company, corporation or other entity
* except as specified in the terms of said license.
*
* No right, title, ownership or other interest in the software is hereby
* granted or transferred.
*
* The information contained herein is subject to change without notice
* and should not be construed as a commitment by NEXELL, Inc.
*
* TITLE		: NXB220 services source file.
*
* FILENAME	: nxb220.c
*
* DESCRIPTION	:
*	Library of routines to initialize, and operate on the NEXELL T-DMB demod.
*
******************************************************************************/
/******************************************************************************
* REVISION HISTORY
*
*    DATE         NAME          REMARKS
* ----------  -------------    ------------------------------------------------
* 03/03/2013  Yang, Maverick       Created.
******************************************************************************/

#include "nxb220_rf.h"
#include "nxb220_internal.h"

U8 g_bRtvIntrMaskReg;
U8 g_bRtvPage;
UINT g_nRtvThresholdSize_FULLSEG;

/* #define INTERFACE_TEST */

/* #define CHECK_REV_NUM */

INT nxtv_InitSystem_FULLSEG(void)
{
	int i;
	U8 read0, read1;
	U8 rev_num;
	U8 ALDO_OUT = 0, DLDO_OUT = 0;
		
#ifdef NXTV_DUAL_DIVERISTY_ENABLE
	U8 S_ALDO_OUT = 0, S_DLDO_OUT = 0;
#endif
	g_bRtvIntrMaskReg = 0x3F;

#if defined(NXTV_IF_SPI) || defined(NXTV_IF_SPI_TSIFx) || defined(NXTV_IF_EBI2)
	#define WR27_VAL	(0x50|SPI_INTR_POL_ACTIVE)
	#define WR29_VAL	0x10

	for (i = 0; i < 100; i++) {
		NXTV_REG_MAP_SEL(SPI_CTRL_PAGE);
		NXTV_REG_SET(0x29, WR29_VAL); /* BUFSEL first! */
		NXTV_REG_SET(0x27, WR27_VAL);

		read0 = NXTV_REG_GET(0x27);
		read1 = NXTV_REG_GET(0x29);

#if defined(NXTV_SPI_HIGH_SPEED_ENABLE)
		NXTV_REG_MAP_SEL(RF_PAGE);
#if (NXTV_SRC_CLK_FREQ_KHz == 19200)
		NXTV_REG_SET(0xB6, 0x04);
		NXTV_REG_SET(0xB6, 0x24); /* DIV8 */
#else
		NXTV_REG_SET(0xB6, 0x05);
		NXTV_REG_SET(0xB6, 0x25); /* DIV8 */
#endif

#endif

		NXTV_REG_MAP_SEL(TOP_PAGE);
		NXTV_REG_SET(0x0C, 0xC3);
		NXTV_DBGMSG2("read0(0x%02X), read1(0x%02X)\n", read0, read1);

		if ((read0 == WR27_VAL) && (read1 == WR29_VAL)) {
			NXTV_REG_MAP_SEL(SPI_CTRL_PAGE);
			NXTV_REG_SET(0x21, 0x87);
			NXTV_REG_SET(0x22, 0x00);
			goto NXTV_POWER_ON_SUCCESS;
		}

		NXTV_DBGMSG1("[nxtv_InitSystem_FULLSEG] Power On wait: %d\n", i);
		NXTV_DELAY_MS(5);
	}
#else
	NXTV_REG_MAP_SEL(TOP_PAGE);
	NXTV_REG_SET(0x0C, 0xC3);
	for (i = 0; i < 100; i++) {
		read0 = NXTV_REG_GET(0x00);
		read1 = NXTV_REG_GET(0x01);
		NXTV_DBGMSG2("read0(0x%02X), read1(0x%02X)\n", read0, read1);

		if ((read0 == 0xC6))
			goto NXTV_POWER_ON_SUCCESS;

		NXTV_DBGMSG1("[nxtv_InitSystem_FULLSEG] Power On wait: %d\n", i);
		NXTV_DELAY_MS(5);
	}
#endif

	NXTV_DBGMSG1("nxtv_InitSystem_FULLSEG: Power On Check error: %d\n", i);
	return NXTV_POWER_ON_CHECK_ERROR;

NXTV_POWER_ON_SUCCESS:


#if 1
	NXTV_REG_MAP_SEL(RF_PAGE);
	rev_num = (NXTV_REG_GET(0x10) & 0xF0) >> 4 ;

#ifdef CHECK_REV_NUM
	NXTV_DBGMSG1("[nxtv_InitSystem_FULLSEG] REV number (%d)\n", rev_num);
#endif
	if (rev_num >= 0x05) {
		NXTV_REG_MASK_SET(0x3B, 0x01, 0x01);
		NXTV_REG_MASK_SET(0x32, 0x01, 0x01);
	}
#endif

#ifdef INTERFACE_TEST
#if defined(NXTV_IF_SPI) || defined(NXTV_IF_SPI_TSIFx) || defined(NXTV_IF_EBI2)
		NXTV_REG_MAP_SEL(SPI_CTRL_PAGE);
		for (i = 0; i < 100; i++) {
			NXTV_REG_SET(0x22, 0x55);
			read0 = NXTV_REG_GET(0x22);
			NXTV_REG_SET(0x22, 0xAA);
			read1 = NXTV_REG_GET(0x22);
	
			NXTV_DBGMSG2("Before Power Setup :readSPI22_55(0x%02X), readSPI22_AA(0x%02X)\n",
						read0, read1);
		}
	
		NXTV_REG_MAP_SEL(RF_PAGE);
		for (i = 0; i < 100; i++) {
			NXTV_REG_SET(0x20, 0x55);
			read0 = NXTV_REG_GET(0x20);
			NXTV_REG_SET(0x20, 0xAA);
			read1 = NXTV_REG_GET(0x20);
	
			NXTV_DBGMSG2("Before Power Setup :readRF20_55(0x%02X), readRF20_AA(0x%02X)\n",
					read0, read1);
		}
#else
		NXTV_REG_MAP_SEL(RF_PAGE);
	
		for (i = 0; i < 100; i++) {
			NXTV_REG_SET(0x20, 0x55);
			read0 = NXTV_REG_GET(0x20);
			NXTV_REG_SET(0x20, 0xAA);
			read1 = NXTV_REG_GET(0x20);
	
			NXTV_DBGMSG2("Before Power Setup :readRF20_55(0x%02X), readRF20_AA(0x%02X)\n",
				read0, read1);
		}
#endif
#endif /* INTERFACE_TEST */


	ALDO_OUT = 6;
	DLDO_OUT = 1;

#ifdef NXTV_DUAL_DIVERISTY_ENABLE
	S_ALDO_OUT = 6;
	S_DLDO_OUT = 1;
#endif

	NXTV_REG_MAP_SEL(RF_PAGE);

#ifdef NXTV_DUAL_DIVERISTY_ENABLE
	if (nxtvNXB220_Get_Diversity_Current_path() == DIVERSITY_MASTER) {
#endif
		NXTV_REG_MASK_SET(0xC8, 0x80, ((ALDO_OUT & 0x04) << 5));
		NXTV_REG_MASK_SET(0xD1, 0x80, ((ALDO_OUT & 0x02) << 6));
		NXTV_REG_MASK_SET(0xD2, 0x80, ((ALDO_OUT & 0x01) << 7));

		NXTV_REG_MASK_SET(0xD3, 0x80, ((DLDO_OUT & 0x04) << 5));
		NXTV_REG_MASK_SET(0xD5, 0x80, ((DLDO_OUT & 0x02) << 6));
		NXTV_REG_MASK_SET(0xD6, 0x80, ((DLDO_OUT & 0x01) << 7));
#ifdef NXTV_DUAL_DIVERISTY_ENABLE
	} else {
		NXTV_REG_MASK_SET(0xC8, 0x80, ((S_ALDO_OUT & 0x04) << 5));
		NXTV_REG_MASK_SET(0xD1, 0x80, ((S_ALDO_OUT & 0x02) << 6));
		NXTV_REG_MASK_SET(0xD2, 0x80, ((S_ALDO_OUT & 0x01) << 7));

		NXTV_REG_MASK_SET(0xD3, 0x80, ((S_DLDO_OUT & 0x04) << 5));
		NXTV_REG_MASK_SET(0xD5, 0x80, ((S_DLDO_OUT & 0x02) << 6));
		NXTV_REG_MASK_SET(0xD6, 0x80, ((S_DLDO_OUT & 0x01) << 7));
	}
#endif

	NXTV_DELAY_MS(10);

	NXTV_REG_MASK_SET(0xC9, 0x80, 0x80);

#if defined(NXTV_EXT_POWER_MODE)
	NXTV_REG_SET(0xCD, 0xCF);
	#ifdef NXTV_DUAL_DIVERISTY_ENABLE
	NXTV_REG_SET(0xCE, 0x35);
	#else
	NXTV_REG_SET(0xCE, 0xB5);
	#endif
#else /* Internal LDO Mode */
	NXTV_REG_SET(0xCD, 0x4F);
	NXTV_REG_SET(0xCE, 0x35);
#endif

#ifdef INTERFACE_TEST
#if defined(NXTV_IF_SPI) || defined(NXTV_IF_SPI_TSIFx) || defined(NXTV_IF_EBI2)

	NXTV_REG_MAP_SEL(SPI_CTRL_PAGE);
	for (i = 0; i < 100; i++) {
		NXTV_REG_SET(0x22, 0x55);
		read0 = NXTV_REG_GET(0x22);
		NXTV_REG_SET(0x22, 0xAA);
		read1 = NXTV_REG_GET(0x22);

		NXTV_DBGMSG2("After Power Setup :readSPI22_55(0x%02X), readSPI22_AA(0x%02X)\n",
					read0, read1);
	}

	NXTV_REG_MAP_SEL(RF_PAGE);
	for (i = 0; i < 100; i++) {
		NXTV_REG_SET(0x20, 0x55);
		read0 = NXTV_REG_GET(0x20);
		NXTV_REG_SET(0x20, 0xAA);
		read1 = NXTV_REG_GET(0x20);

		NXTV_DBGMSG2("After Power Setup :readRF20_55(0x%02X), readRF20_AA(0x%02X)\n",
				read0, read1);
	}
#else
	NXTV_REG_MAP_SEL(RF_PAGE);

	for (i = 0; i < 100; i++) {
		NXTV_REG_SET(0x20, 0x55);
		read0 = NXTV_REG_GET(0x20);
		NXTV_REG_SET(0x20, 0xAA);
		read1 = NXTV_REG_GET(0x20);

		NXTV_DBGMSG2("After Power Setup :readRF20_55(0x%02X), readRF20_AA(0x%02X)\n",
			read0, read1);
	}
#endif
#endif /* INTERFACE_TEST */

	return NXTV_SUCCESS;
}
