/** @file

PE PCI Handler

This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "StmRuntime.h"
#include "PeStm.h"
#include "Library/PciExpressLib.h"

void SetTimerRate(UINT16 value);

#define D31F0_PMBASE       0x40
#define D31F0_GEN_PMCON_1  0xA0
#define D31F0_GEN_PMCON_3  0xA4

#define SMI_EN   0x30         /* SMI Control and Enable Register */
#define SWSMI_TMR_EN  (1<<6)  // start software timer on bit set
#define PERIODIC_EN   (1<<14)
#define EOS_EN        (1<<1)  /* End of SMI  */

#define SMI_STS  0x34		/* SMI Status Register  */

// SMI STATUS BITS

#define SWSMI_TMR_STS (1<<6) 
#define PERIODIC_STS  (1<<14)

void PrintSmiEnRegister (UINT32 Index);
extern STM_GUEST_CONTEXT_COMMON        mGuestContextCommonSmm[];

typedef int device_t;

static UINT16 pmbase = 0x0;


UINT16 get_pmbase (void)
{
	if (pmbase == 0)
	{
#ifdef CONFIG_PMBASE
		pmbase = CONFIG_PMBASE;
		DEBUG ((EFI_D_INFO,
			"get_pmbase - (from CONFIG_PMBASE) set at 0x%x\n",
			pmbase));
#else
#ifdef PMBASE_BIOS_RESOURCE
		// find the pmbase in the BIOS resource list

		STM_RSC *Resource;

		// the pmbase is the first IO resource
		Resource = GetStmFirstResource (
				(STM_RSC *)mGuestContextCommonSmm[SMI_HANDLER].BiosHwResourceRequirementsPtr,
				IO_RANGE);
		if (Resource == NULL)
			DEBUG ((EFI_D_ERROR,
				"get_pmbase - Error pmbase not found in resource list\n"));
		else
		{
			pmbase = Resource->Io.Base;
			DEBUG ((EFI_D_INFO,
				"get_pmbase - (from BIOS RSC) pmbase set at 0x%x\n",
				pmbase));
		}
#else
	        pmbase = PciExpressRead16 (PCI_EXPRESS_LIB_ADDRESS (0, 0x1f, 0, D31F0_PMBASE));

		if ((pmbase & 0x1) != 0x1)
		{
			DEBUG((EFI_D_ERROR,
				"get_pmbase - Error in pmbase value: 0x%x\n",
				pmbase));
			pmbase = 0;
		}

		pmbase &= 0xFFFC;

	        DEBUG ((EFI_D_INFO,
                                "get_pmbase - (from PCI Memory) pmbase set at 0x%x\n",
				pmbase));
#endif
#endif
        }
	return pmbase;
} 

void StartTimer (void)
{
	UINT16 pmbase = get_pmbase();
	UINT32 smi_en = IoRead32(pmbase + SMI_EN);
	UINT32 smi_sts = IoRead32(pmbase + SMI_STS);

	smi_en |= PERIODIC_EN;
#if 0
	DEBUG ((EFI_D_INFO,
		"StartTimer - smi_en: 0x%08lx smi_sts: 0x%08lx\n",
		smi_en,
		smi_sts));
#endif
	IoWrite32 (pmbase + SMI_STS, PERIODIC_STS);
	IoWrite32 (pmbase + SMI_EN, smi_en);
}

void SetEndOfSmi (void)
{
	UINT16 pmbase = get_pmbase ();
	UINT32 smi_en = IoRead32 (pmbase + SMI_EN);
	smi_en |= EOS_EN;  // set the bit
#if 0
	DEBUG ((EFI_D_INFO,
		"-- SetEndOfSmi pmbase: %x smi_en: %x \n",
		pmbase,
		smi_en));
#endif
	IoWrite32 (pmbase + SMI_EN, smi_en);
#if 0
	DEBUG ((EFI_D_INFO,
		"SetEndOfSmi smi_en: 0x%08lx smi_sts: 0x%08lx\n",
		IoRead32 (pmbase + SMI_EN),
		IoRead32 (pmbase + SMI_STS)));
#endif
}

void PrintSmiEnRegister (UINT32 Index)
{
	UINT16 pmbase = get_pmbase ();
	DEBUG ((EFI_D_INFO,
		"%ld PrintSmiEnRegister smi_en: 0x%08x smi_sts: 0x%08x\n",
		Index,
		IoRead32 (pmbase + SMI_EN),
		IoRead32 (pmbase + SMI_STS)));
}

void AckTimer (void)
{
	UINT16 pmbase = get_pmbase ();

	IoWrite32 (pmbase + SMI_STS, PERIODIC_STS);
#if 0
	DEBUG ((EFI_D_INFO,
		"AckTimer - smi_en: 0x%08lx smi_sts: 0x%08lx\n",
		IoRead32 (pmbase + SMI_EN),
		IoRead32 (pmbase + SMI_STS)));
#endif
}

void StopSwTimer (void)
{
	UINT16 pmbase = get_pmbase ();
	UINT32 smi_en = IoRead32 (pmbase + SMI_EN);

	smi_en &= ~PERIODIC_EN;
	IoWrite32 (pmbase + SMI_EN, smi_en);
}

/*
 *  CheckTimerSTS
 *   Input:
 *     Index - cpu number
 *
 *   Output:
 *     0 - No timer interrupt detected
 *     1 - Timer interrupt detected
 *     2 - Timer interrupt plus additional SMI
 */

int CheckTimerSTS (UINT32 Index)
{
	UINT16 pmbase = get_pmbase ();
	UINT32 smi_sts = IoRead32 (pmbase + SMI_STS);
#if 0
	DEBUG((EFI_D_ERROR, "%ld CheckTimerSTS - 0x%08lx\n", Index, smi_sts));
#endif
	if ((smi_sts & PERIODIC_STS) == PERIODIC_STS)
	{
		UINT32 smi_en = IoRead32 (pmbase + SMI_EN);
		UINT32 other_smi = (smi_en & smi_sts) & ~PERIODIC_STS;

		if (other_smi == 0)
		{ 
			DEBUG ((EFI_D_INFO,
				"%ld CheckTimerSTS - Timer Interrupt Detected\n",
				Index,
				smi_sts));
			return 1;
		}
		else
		{
			DEBUG ((EFI_D_INFO,
				"%ld CheckTimerSTS - Timer + other SMI found\n",
				Index,
				smi_sts));
			return 2;
		}
	}
	else
	{
#if 0
		DEBUG ((EFI_D_INFO,
			"%ld CheckTimerSTS - No Timer Interrupt Detected\n",
			Index,
			smi_sts));
#endif
		return 0;
	}
}

void ClearTimerSTS ()
{
	UINT16 pmbase = get_pmbase ();
	
	// just want to clear the  status - do not touch the rest
	IoWrite32 (pmbase + SMI_STS, PERIODIC_STS);
}

void SetMaxSwTimerInt ()
{
	SetTimerRate (0);
}

void SetMinSwTimerInt ()
{
	SetTimerRate (3);
}

void SetTimerRate (UINT16 value)
{
	UINT16 Reg16;
	UINT16 TimeOut;

	if (value > 3)
	{
		value = 3;
	}
	TimeOut = (value << 0);

	Reg16 = PciExpressRead16 (PCI_EXPRESS_LIB_ADDRESS (0, 0x1f, 0, D31F0_GEN_PMCON_1));
	PciExpressWrite16 (PCI_EXPRESS_LIB_ADDRESS (0, 0x1f, 0, D31F0_GEN_PMCON_1), Reg16|TimeOut);
}
