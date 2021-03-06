/** @file
 SMM EPT handler
 
  Copyright (c) 2015 - 2016, Intel Corporation. All rights reserved.<BR>
 This program and the accompanying materials
 are licensed and made available under the terms and conditions of the BSD License
 which accompanies this distribution.  The full text of the license may be found at
 http://opensource.org/licenses/bsd-license.php.
 
 THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
 
 **/

#include "StmRuntime.h"
#include "PeStm.h"

#define BUS_FROM_PCIE_ADDRESS(PcieAddress)       (UINT8)(((UINTN)(PcieAddress) & 0x0FF00000) >> 20)
#define DEVICE_FROM_PCIE_ADDRESS(PcieAddress)    (UINT8)(((UINTN)(PcieAddress) & 0x000F8000) >> 15)
#define FUNCTION_FROM_PCIE_ADDRESS(PcieAddress)  (UINT8)(((UINTN)(PcieAddress) & 0x00007000) >> 12)
#define REGISTER_FROM_PCIE_ADDRESS(PcieAddress)  (UINT16)((UINTN)(PcieAddress) & 0x00000FFF)

#define PAGE_PROGATE_BITS           (BIT0 | BIT1 | BIT2 | BIT3 | BIT4 | BIT5)

#define PAGING_4K_MASK  0xFFF
#define PAGING_2M_MASK  0x1FFFFF
#define PAGING_1G_MASK  0x3FFFFFFF

#define PAGING_PAE_INDEX_MASK  0x1FF

#define PAGING_4K_ADDRESS_MASK_64 0x000FFFFFFFFFF000ull
#define PAGING_2M_ADDRESS_MASK_64 0x000FFFFFFFE00000ull
#define PAGING_1G_ADDRESS_MASK_64 0x000FFFFFC0000000ull

typedef enum {
  PageNone,
  Page4K,
  Page2M,
  Page1G,
} PAGE_ATTRIBUTE;

typedef struct {
  PAGE_ATTRIBUTE   Attribute;
  UINT64           Length;
  UINT64           AddressMask;
} PAGE_ATTRIBUTE_TABLE;

PAGE_ATTRIBUTE_TABLE mPageAttributeTable[] = {
  {Page4K,  SIZE_4KB, PAGING_4K_ADDRESS_MASK_64},
  {Page2M,  SIZE_2MB, PAGING_2M_ADDRESS_MASK_64},
  {Page1G,  SIZE_1GB, PAGING_1G_ADDRESS_MASK_64},
};

extern UINT64
EptAllocatePte(
               IN UINT64 EptPointer,
               IN UINT64 BaseAddress,
               IN UINT64 Length,
			   IN UINT64 PhySize
               );

extern UINT8 GetMemoryType (IN UINT64 BaseAddress);

/**
  Return length according to page attributes.

  @param[in]  PageAttributes   The page attribute of the page entry.

  @return The length of page entry.
**/
UINTN
PageAttributeToLength (
  IN PAGE_ATTRIBUTE  PageAttribute
  )
{
  UINTN  Index;
  for (Index = 0; Index < sizeof(mPageAttributeTable)/sizeof(mPageAttributeTable[0]); Index++) {
    if (PageAttribute == mPageAttributeTable[Index].Attribute) {
      return (UINTN)mPageAttributeTable[Index].Length;
    }
  }
  return 0;
}

/**
  Return address mask according to page attributes.

  @param[in]  PageAttributes   The page attribute of the page entry.

  @return The address mask of page entry.
**/
UINTN
PageAttributeToMask (
  IN PAGE_ATTRIBUTE  PageAttribute
  )
{
  UINTN  Index;
  for (Index = 0; Index < sizeof(mPageAttributeTable)/sizeof(mPageAttributeTable[0]); Index++) {
    if (PageAttribute == mPageAttributeTable[Index].Attribute) {
      return (UINTN)mPageAttributeTable[Index].AddressMask;
    }
  }
  return 0;
}

/**
  Return page table entry to match the address.

  @param[in]   Address          The address to be checked.
  @param[out]  PageAttributes   The page attribute of the page entry.

  @return The page entry.
**/
EPT_ENTRY *
GetPageTableEntry (
  IN  UINT64                            EptPointer,
  IN  PHYSICAL_ADDRESS                  Address,
  IN  PHYSICAL_ADDRESS					PhyAddress,
  IN  UINT64                            PhySize,
  OUT PAGE_ATTRIBUTE                    *PageAttribute
  )
{
  UINTN                 Index1;
  UINTN                 Index2;
  UINTN                 Index3;
  UINTN                 Index4;
  EPT_ENTRY             *L1PageTable;
  EPT_ENTRY             *L2PageTable;
  EPT_ENTRY             *L3PageTable;
  EPT_ENTRY             *L4PageTable;

  //DEBUG ((EFI_D_INFO, "GetPageTableEntry: Address(guest) %llx Address(Physical) %llx Size %llx\n", (UINTN)Address, (UINTN) PhyAddress, (UINTN) PhySize));

  Index4 = ((UINTN)RShiftU64 (Address, 39)) & PAGING_PAE_INDEX_MASK;
  Index3 = ((UINTN)Address >> 30) & PAGING_PAE_INDEX_MASK;
  Index2 = ((UINTN)Address >> 21) & PAGING_PAE_INDEX_MASK;
  Index1 = ((UINTN)Address >> 12) & PAGING_PAE_INDEX_MASK;
  //DEBUG ((EFI_D_INFO, "Index: %x %x %x %x\n", Index4, Index3, Index2, Index1));

    L4PageTable = (EPT_ENTRY *)(UINTN) EptPointer; //(mGuestContextCommonSmm.EptPointer.Uint64 & PAGING_4K_ADDRESS_MASK_64);
  if (L4PageTable[Index4].Uint64 == 0) {
    if(EptAllocatePte(EptPointer, Address, PhyAddress, PhySize) == 0)
    {
        *PageAttribute = PageNone;
        return NULL;
    }
  }

  L3PageTable = (EPT_ENTRY *)(UINTN)(L4PageTable[Index4].Uint64 & PAGING_4K_ADDRESS_MASK_64);
  if (L3PageTable[Index3].Uint64 == 0) {
      if(EptAllocatePte(EptPointer, Address, PhyAddress, PhySize) == 0)
      {
          *PageAttribute = PageNone;
          return NULL;
      }
  }
  if (L3PageTable[Index3].Bits32.Sp != 0) {
    // 1G
    *PageAttribute = Page1G;
    return &L3PageTable[Index3];
  }

  L2PageTable = (EPT_ENTRY *)(UINTN)(L3PageTable[Index3].Uint64 & PAGING_4K_ADDRESS_MASK_64);
  if (L2PageTable[Index2].Uint64 == 0) {
       if(EptAllocatePte(EptPointer, Address, PhyAddress, PhySize) == 0)
       {
           *PageAttribute = PageNone;
           return NULL;
       }
  }
  if (L2PageTable[Index2].Bits32.Sp != 0) {
    // 2M
    *PageAttribute = Page2M;
    return &L2PageTable[Index2];
  }

  // Special case where there exists a L1 Page Table and the caller wants the range to be 2M
  // If the stars line up (both Address and PhyAddress are on 2MB boundries), then
  // free the L1 Page table and replace it with a super page

  if ((Address >= BASE_2MB) &&
                ((Address & PAGING_2M_MASK) == 0) &&
				((PhyAddress & PAGING_2M_MASK) == 0) &&
				(PhySize > PAGING_2M_MASK))
  {
	  //DEBUG((EFI_D_INFO, "GetPageTableEntry: replacing L1PageTable: 0x%llx with superpage\n",  L2PageTable[Index2].Uint64));
	  FreePages((UINT64 *)((UINTN)(L2PageTable[Index2].Uint64 & PAGING_4K_ADDRESS_MASK_64)), 1); // free up the L2

	  L2PageTable[Index2].Uint64 = 0;
	  if(EptAllocatePte(EptPointer, Address, PhyAddress, PhySize) == 0)
       {
           *PageAttribute = PageNone;
           return NULL;
       }
	  *PageAttribute = Page2M;
    return &L2PageTable[Index2];
  }

  // 4k
  L1PageTable = (EPT_ENTRY *)(UINTN)(L2PageTable[Index2].Uint64 & PAGING_4K_ADDRESS_MASK_64);
  if ((L1PageTable[Index1].Uint64 == 0) && (Address != 0)) {
      if(EptAllocatePte(EptPointer, Address, PhyAddress, PhySize) == 0){
          *PageAttribute = PageNone;
          return NULL;
      }
  }
  *PageAttribute = Page4K;
  return &L1PageTable[Index1];
}

/**
  Modify memory attributes of page entry.

  @param[in]   PageEntry        The page entry.
  @param[in]   Attributes       The bit mask of attributes to modify for the memory region.
  @param[in]   IsSet            TRUE means to set attributes. FALSE means to clear attributes.
  @param[out]  IsModified       TRUE means page table modified. FALSE means page table not modified.
**/
VOID
ConvertPageEntryAttribute (
  IN EPT_ENTRY                          *PageEntry,
  IN UINT32                             Ra,
  IN UINT32                             Wa,
  IN UINT32                             Xa,
  IN EPT_PAGE_ATTRIBUTE_SETTING         EptPageAttributeSetting,
  IN UINT32								EMT
  )
{
  UINT64  CurrentPageEntry;

  CurrentPageEntry = PageEntry->Uint64;
  switch (EptPageAttributeSetting) {
  case EptPageAttributeSet:
    PageEntry->Bits32.Ra = (UINT32)Ra;
    PageEntry->Bits32.Wa = (UINT32)Wa;
    PageEntry->Bits32.Xa = (UINT32)Xa;
    break;
  case EptPageAttributeAnd:
    PageEntry->Bits32.Ra &= (UINT32)Ra;
    PageEntry->Bits32.Wa &= (UINT32)Wa;
    PageEntry->Bits32.Xa &= (UINT32)Xa;
    break;
  case EptPageAttributeOr:
    PageEntry->Bits32.Ra |= (UINT32)Ra;
    PageEntry->Bits32.Wa |= (UINT32)Wa;
    PageEntry->Bits32.Xa |= (UINT32)Xa;
    break;
  default:
    CpuDeadLoop ();
    break;
  }

  if(PageEntry->Bits32.Wa == 1)
	  PageEntry->Bits.Ra = 1;    // have to have read with write(vol 3c 28.2.3.1)
  if(PageEntry->Bits32.Xa == 1)
	  PageEntry->Bits32.Ra = 1; //have to have read with execute (except for some processors)

  if(EMT == -1)
	  PageEntry->Bits32.Emt = GetMemoryType((PageEntry->Uint64) & PAGING_4K_MASK );
  else
	  PageEntry->Bits32.Emt =  EMT;   // let caller set the memory type (at his own risk)

#ifdef DEBUGPRINT
  DEBUG((EFI_D_INFO, "ConvertPageEntryAttribute - 0x%llx->0x%llx (%1x%1x%1x) Emt: %3x\n",
	  (UINT64) CurrentPageEntry, 
	  PageEntry->Uint64,
	  PageEntry->Bits32.Ra,
	  PageEntry->Bits32.Wa,
	  PageEntry->Bits32.Xa,
	  PageEntry->Bits32.Emt));
#endif
  if (CurrentPageEntry != PageEntry->Uint64) {
   //DEBUG ((EFI_D_INFO, "ConvertPageEntryAttribute 0x%lx->0x%lx\n", CurrentPageEntry, PageEntry->Uint64));
  }
}

/**
  This function returns if there is need to split page entry.

  @param[in]  BaseAddress      The base address to be checked.
  @param[in]  Length           The length to be checked.
  @param[in]  PageAttribute    The page attribute of the page entry.

  @retval SplitAttributes on if there is need to split page entry.
**/
PAGE_ATTRIBUTE
NeedSplitPage (
  IN  PHYSICAL_ADDRESS                  BaseAddress,
  IN  UINT64                            Length,
  IN  PAGE_ATTRIBUTE                    PageAttribute
  )
{
  UINT64                PageEntryLength;

  PageEntryLength = PageAttributeToLength (PageAttribute);

  if (((BaseAddress & (PageEntryLength - 1)) == 0) && (Length >= PageEntryLength)) {
    return PageNone;
  }

  if (((BaseAddress & PAGING_2M_MASK) != 0) || (Length < SIZE_2MB)) {
    return Page4K;
  }

  return Page2M;
}

/**
  This function splits one page entry to small page entries.

  @param[in]  PageEntry        The page entry to be splitted.
  @param[in]  PageAttribute    The page attribute of the page entry.
  @param[in]  SplitAttribute   How to split the page entry.

  @retval RETURN_SUCCESS            The page entry is splitted.
  @retval RETURN_UNSUPPORTED        The page entry does not support to be splitted.
  @retval RETURN_OUT_OF_RESOURCES   No resource to split page entry.
**/
RETURN_STATUS
SplitPage (
  IN  EPT_ENTRY                         *PageEntry,
  IN  PAGE_ATTRIBUTE                    PageAttribute,
  IN  PAGE_ATTRIBUTE                    SplitAttribute
  )
{
  UINT64      BaseAddress;
  EPT_ENTRY   *NewPageEntry;
  UINTN       Index;

  ASSERT (PageAttribute == Page2M || PageAttribute == Page1G);

  if (PageAttribute == Page2M) {
    //
    // Split 2M to 4K
    //
    ASSERT (SplitAttribute == Page4K);
    if (SplitAttribute == Page4K) {
      NewPageEntry = (EPT_ENTRY *)AllocatePages (1);
      //DEBUG ((EFI_D_INFO, "Split - 0x%x\n", NewPageEntry));
      if (NewPageEntry == NULL) {
        return RETURN_OUT_OF_RESOURCES;
      }
      BaseAddress = PageEntry->Uint64 & PAGING_2M_ADDRESS_MASK_64;
      for (Index = 0; Index < SIZE_4KB / sizeof(UINT64); Index++) {
        NewPageEntry[Index].Uint64 = BaseAddress + SIZE_4KB * Index + (PageEntry->Uint64 & PAGE_PROGATE_BITS);
      }
      PageEntry->Uint64 = (UINT64)(UINTN)NewPageEntry;
      PageEntry->Bits32.Ra = 1;
      PageEntry->Bits32.Wa = 1;
      PageEntry->Bits32.Xa = 1;
      return RETURN_SUCCESS;
    } else {
      return RETURN_UNSUPPORTED;
    }
  } else if (PageAttribute == Page1G) {
    //
    // Split 1G to 2M
    // No need support 1G->4K directly, we should use 1G->2M, then 2M->4K to get more compact page table.
    //
    ASSERT (SplitAttribute == Page2M || SplitAttribute == Page4K);
    if ((SplitAttribute == Page2M || SplitAttribute == Page4K)) {
      NewPageEntry = (EPT_ENTRY *)AllocatePages (1);
      DEBUG ((EFI_D_INFO, "Split - 0x%x\n", NewPageEntry));
      if (NewPageEntry == NULL) {
        return RETURN_OUT_OF_RESOURCES;
      }
      BaseAddress = PageEntry->Uint64 & PAGING_1G_ADDRESS_MASK_64;
      for (Index = 0; Index < SIZE_4KB / sizeof(UINT64); Index++) {
        NewPageEntry[Index].Uint64    = BaseAddress + SIZE_2MB * Index + (PageEntry->Uint64 & PAGE_PROGATE_BITS);
        NewPageEntry[Index].Bits32.Sp = 1;
      }
      PageEntry->Uint64 = (UINT64)(UINTN)NewPageEntry;
      PageEntry->Bits32.Ra = 1;
      PageEntry->Bits32.Wa = 1;
      PageEntry->Bits32.Xa = 1;
      return RETURN_SUCCESS;
    } else {
      return RETURN_UNSUPPORTED;
    }
  } else {
    return RETURN_UNSUPPORTED;
  }
}

/**

  This function translate guest physical address to host address.

  @param Addr           Guest physical address
  @param EntryPtr       EPT entry pointer
                        NULL on output means Entry not found.

  @return Host physical address
**/
UINTN
TranslateEPTGuestToHost (
                         IN UINT64      EptPointer,
                         IN UINTN       Addr,
                         OUT EPT_ENTRY  **EntryPtr  OPTIONAL
                         );

/**
 
 This function translate guest physical address to host address.
 
 @param EptPointer           EPT pointer
 @param GuestPhysicalAddress Guest physical address
 @param HostPhysicalAddress  Host physical address
 
 @retval TRUE  HostPhysicalAddress is found
 @retval FALSE HostPhysicalAddress is not found
 **/
BOOLEAN
LookupSmiGuestPhysicalToHostPhysical (
                                      IN  UINT64  EptPointer,
                                      IN  UINTN   GuestPhysicalAddress,
                                      OUT UINTN   *HostPhysicalAddress
                                      )
{
    EPT_ENTRY  *EptEntry;
    
    EptEntry = NULL;
    *HostPhysicalAddress = TranslateEPTGuestToHost (EptPointer, GuestPhysicalAddress, &EptEntry);
    if (EptEntry == NULL) {
        return FALSE;
    } else {
        return TRUE;
    }
}

/**
 
 This function translate guest linear address to host address.
 
 @param CpuIndex           CPU index
 @param GuestLinearAddress Guest linear address
 
 @return Host physical address
 **/
UINTN
GuestLinearToHostPhysical (
                           IN UINT32  CpuIndex,
                           IN UINTN   GuestLinearAddress
                           )
{
    UINTN   GuestPhysicalAddress;
    UINT32 VmType = mHostContextCommon.HostContextPerCpu[CpuIndex].GuestVmType;
    
    GuestPhysicalAddress = (UINTN)GuestLinearToGuestPhysical (CpuIndex, GuestLinearAddress);
    return TranslateEPTGuestToHost (mGuestContextCommonSmm[VmType].EptPointer.Uint64, GuestPhysicalAddress, NULL);
}

/**
 
 This function translate guest physical address to host address.
 
 @param EptPointer     EPT pointer
 @param Addr           Guest physical address
 @param EntryPtr       EPT entry pointer.
 NULL on output means Entry not found.
 
 @return Host physical address
 **/
UINTN
TranslateEPTGuestToHost (
                         IN UINT64      EptPointer,
                         IN UINTN       Addr,
                         OUT EPT_ENTRY  **EntryPtr  OPTIONAL
                         )
{
    EPT_ENTRY                *L1PageTable;
    EPT_ENTRY                *L2PageTable;
    EPT_ENTRY                *L3PageTable;
    EPT_ENTRY                *L4PageTable;
    UINTN                    Index1;
    UINTN                    Index2;
    UINTN                    Index3;
    UINTN                    Index4;
    UINTN                    Offset;
    
    Index4 = ((UINTN)RShiftU64 (Addr, 39)) & 0x1ff;
    Index3 = ((UINTN)Addr >> 30) & 0x1ff;
    Index2 = ((UINTN)Addr >> 21) & 0x1ff;
    Index1 = ((UINTN)Addr >> 12) & 0x1ff;
    Offset = ((UINTN)Addr & 0xFFF);
    
    if (EntryPtr != NULL) {
        *EntryPtr = NULL;
    }
    L4PageTable = (EPT_ENTRY *)(UINTN)((UINTN)EptPointer & PAGING_4K_ADDRESS_MASK_64);
    if ((L4PageTable[Index4].Bits32.Ra == 0) &&
        (L4PageTable[Index4].Bits32.Wa == 0) &&
        (L4PageTable[Index4].Bits32.Xa == 0)) {
        if (EntryPtr != NULL) {
            *EntryPtr = &L4PageTable[Index4];
        }
        return 0;
    }
  L3PageTable = (EPT_ENTRY *)(UINTN)((UINTN)L4PageTable[Index4].Uint64 & PAGING_4K_ADDRESS_MASK_64);
    if ((L3PageTable[Index3].Bits32.Ra == 0) &&
        (L3PageTable[Index3].Bits32.Wa == 0) &&
        (L3PageTable[Index3].Bits32.Xa == 0)) {
        if (EntryPtr != NULL) {
            *EntryPtr = &L3PageTable[Index3];
        }
        return 0;
    }
    if (L3PageTable[Index2].Bits32.Sp == 1) {
        if (EntryPtr != NULL) {
            *EntryPtr = &L3PageTable[Index3];
        }
        return ((UINTN)L3PageTable[Index3].Uint64 & PAGING_1G_ADDRESS_MASK_64) + ((UINTN)Addr & PAGING_1G_MASK);
    }
    
  L2PageTable = (EPT_ENTRY *)(UINTN)((UINTN)L3PageTable[Index3].Uint64 & PAGING_4K_ADDRESS_MASK_64);
    if ((L2PageTable[Index2].Bits32.Ra == 0) &&
        (L2PageTable[Index2].Bits32.Wa == 0) &&
        (L2PageTable[Index2].Bits32.Xa == 0)) {
        if (EntryPtr != NULL) {
            *EntryPtr = &L2PageTable[Index2];
        }
        return 0;
    }
    
    if (L2PageTable[Index2].Bits32.Sp == 1) {
        if (EntryPtr != NULL) {
            *EntryPtr = &L2PageTable[Index2];
        }
        return ((UINTN)L2PageTable[Index2].Uint64 & PAGING_2M_ADDRESS_MASK_64) + ((UINTN)Addr & PAGING_2M_MASK);
    }
    
  L1PageTable = (EPT_ENTRY *)(UINTN)((UINTN)L2PageTable[Index2].Uint64 & PAGING_4K_ADDRESS_MASK_64);
    if ((L1PageTable[Index1].Bits32.Ra == 0) &&
        (L1PageTable[Index1].Bits32.Wa == 0) &&
        (L1PageTable[Index1].Bits32.Xa == 0)) {
        // not check last one, since user may update it
        //    return 0;
    }
    
    if (EntryPtr != NULL) {
        *EntryPtr = &L1PageTable[Index1];
    }
    return ((UINTN)L1PageTable[Index1].Uint64 & PAGING_4K_ADDRESS_MASK_64) + Offset;
}

/**
 
 This function set EPT page table attribute by range.
 
 @param Base                     Memory base
 @param Length                   Memory length
 @param Ra                       Read access
 @param Wa                       Write access
 @param Xa                       Execute access
 @param EptPageAttributeSetting  EPT page attribute setting
 
 STM/PE note:
 
 **/
RETURN_STATUS
EPTSetPageAttributeRange (
                          IN UINT64                     EptPointerIN,
                          IN UINT64                     Base,
                          IN UINT64                     Length,
                          IN UINT64                     PhysMem,
                          IN UINT32                     Ra,
                          IN UINT32                     Wa,
                          IN UINT32                     Xa,
                          IN EPT_PAGE_ATTRIBUTE_SETTING EptPageAttributeSetting,
						  IN INT32						EMT
                          )
{
  EPT_ENTRY                         *PageEntry;
  PAGE_ATTRIBUTE                    PageAttribute;
  UINTN                             PageEntryLength;
  PAGE_ATTRIBUTE                    SplitAttribute;
  RETURN_STATUS                     Status;
  UINT_128   Data128;
  UINT64							EptPointer;
  UINT64							Offset;       // offset of address into the page
  UINT64							OLength;      // Length plus offset
#if 0
  DEBUG ((EFI_D_INFO, "EPTSetPageAttributeRange - Base: 0x%016lx - Length: 0x%016lx - PhysMem: 0x%016lx (%1x%1x%1x) EMT:%d\n", Base, Length, PhysMem, Ra, Wa,Xa, EMT));
#endif  
    // assumption, the user does not change PhysMem on us
    
  EptPointer = EptPointerIN & PAGING_4K_ADDRESS_MASK_64;  // make sure we have only the address

  // Normalize the request to page boundries and lengths

  Offset = Base & PAGING_4K_MASK;
  OLength = Length + Offset;       // actual length based on the start of the page 
  Length = OLength & PAGING_4K_ADDRESS_MASK_64;

 if(((OLength & PAGING_4K_MASK) > 0) ||
	 (Length == 0))
 {
	 Length = Length + SIZE_4KB;
 }

 Base &= PAGING_4K_ADDRESS_MASK_64;
 PhysMem &= PAGING_4K_ADDRESS_MASK_64;

  while (Length != 0) {
    PageEntry = GetPageTableEntry (EptPointer, Base, PhysMem, Length, &PageAttribute);
//	DEBUG((EFI_D_INFO, "EPTSetPageAttributeRange - received page entry: %llx\n", PageEntry->Uint64));
    if (PageEntry == NULL) {
      DEBUG ((EFI_D_INFO, "EPTSetPageAttributeRange - PageEntry == NULL\n"));
      return RETURN_UNSUPPORTED;
    }
    PageEntryLength = PageAttributeToLength (PageAttribute);
    SplitAttribute = NeedSplitPage (Base, Length, PageAttribute);
    if (SplitAttribute == PageNone) {
      ConvertPageEntryAttribute (PageEntry, Ra, Wa, Xa, EptPageAttributeSetting, EMT);
      //
      // Convert success, move to next
      //
      Base += PageEntryLength;
      PhysMem += PageEntryLength;
      Length -= PageEntryLength;
        } else {
      Status = SplitPage (PageEntry, PageAttribute, SplitAttribute);
      if (RETURN_ERROR (Status)) {
        DEBUG ((EFI_D_INFO, "EPTSetPageAttributeRange - SplitPage Error - %r\n", Status));
        return RETURN_UNSUPPORTED;
      }
      //
      // Just split current page
      // Convert success in next around
      //
    }
  }

  Data128.Lo = EptPointerIN;
  Data128.Hi = 0;
  AsmInvEpt (INVEPT_TYPE_SINGLE_CONTEXT_INVALIDATION, &Data128);

  //DEBUG ((EFI_D_INFO, "EPTSetPageAttributeRange - %r\n", RETURN_SUCCESS));
  return RETURN_SUCCESS;
}

/**
 
 This function is EPT violation handler for SMM.
 
 @param Index CPU index
 
 **/

extern unsigned int StmVmPeNmiExCount;
VOID
SmmEPTViolationHandler (
                        IN UINT32 Index
                        )
{
    VM_EXIT_QUALIFICATION   Qualification;
    STM_RSC_MEM_DESC        *MemDesc;
    UINT64                  Address;
    STM_RSC_PCI_CFG_DESC    *PciCfgDesc;
    UINT64                  PciExpressAddress;
    STM_RSC_MEM_DESC        LocalMemDesc;
    STM_RSC_PCI_CFG_DESC    *LocalPciCfgDescPtr;
    UINT8                   LocalPciCfgDescBuf[STM_LOG_ENTRY_SIZE];
    UINT32				  VmType = SMI_HANDLER;
    
    Qualification.UintN = VmReadN (VMCS_N_RO_EXIT_QUALIFICATION_INDEX);
    
    DEBUG ((EFI_D_ERROR, "%ld !!!EPTViolationHandler!!!\n", (UINTN)Index));
    DEBUG ((EFI_D_ERROR, "%ld  Qualification - %016lx\n", (UINTN) Index, (UINT64)Qualification.UintN));
    DEBUG ((EFI_D_ERROR, "%ld  GuestPhysicalAddress - %016lx\n", (UINTN) Index, VmRead64 (VMCS_64_RO_GUEST_PHYSICAL_ADDR_INDEX)));
    
    StmVmPeNmiExCount++;   // make sure there is no smi processors waiting
    
    if (Qualification.EptViolation.GlaValid == 0) {
        //
        // 0=Linear address invalid.
        //
		DEBUG ((EFI_D_ERROR, "%ld SmmEPTViolationHandler - Linear address invalid\n", Index));
    } else {
        if (Qualification.EptViolation.Gpa == 0) {
            //
            // 1=Linear address valid but does not match provided physical address. EPT violation occurred while performing a guest page walk.
            //   1) No-read EPT page encountered when trying to read from the guest IA32 page tables (e.g fetching a PML4, PDE, PTE).
            //   2) No-write EPT page encountered when trying to write an A or D bit.
            //

			DEBUG ((EFI_D_ERROR, "%ld SmmEPTViolationHandler - EPT violation occurred while performing a guest page walk\n", Index));

        } else {
            //
            // 3=Linear address valid and match provided physical address. This is the normal case.
            //
            Address = VmRead64 (VMCS_64_RO_GUEST_PHYSICAL_ADDR_INDEX);
            MemDesc = GetStmResourceMem (
                                         mHostContextCommon.MleProtectedResource.Base,
                                         Address,
                                         (UINT32)(Qualification.UintN & 0x7)
                                         );
            if (MemDesc != NULL) {
                DEBUG ((EFI_D_ERROR, "%ld SmmEPTViolationHandler - SMI Handler attempted to access MLE protected resource\n", Index));
                AddEventLogForResource (EvtHandledProtectionException, (STM_RSC *)MemDesc);
                SmmExceptionHandler (Index);
                CpuDeadLoop ();
            }
            
            MemDesc = GetStmResourceMem (
                                         (STM_RSC *)(UINTN)mGuestContextCommonSmm[VmType].BiosHwResourceRequirementsPtr,
                                         Address,
                                         (UINT32)(Qualification.UintN & 0x7)
                                         );
            if (MemDesc == NULL) {
                DEBUG((EFI_D_ERROR, "%ld SmmEPTViolationHandler - Add unclaimed MEM_RSC!\n", Index));
                ZeroMem (&LocalMemDesc, sizeof(LocalMemDesc));
                LocalMemDesc.Hdr.RscType = MEM_RANGE;
                LocalMemDesc.Hdr.Length = sizeof(LocalMemDesc);
                LocalMemDesc.Base = Address;
                LocalMemDesc.Length = 1;
                LocalMemDesc.RWXAttributes = (UINT8)(Qualification.UintN & 0x7);
                AddEventLogForResource (EvtBiosAccessToUnclaimedResource, (STM_RSC *)&LocalMemDesc);
                // BUGBUG: it should not happen?
                // TBD: We need create EPT mapping here, if so?

				if(EPTSetPageAttributeRange (
					mGuestContextCommonSmm[SMI_HANDLER].EptPointer.Uint64,
					LocalMemDesc.Base,
					LocalMemDesc.Length,
					LocalMemDesc.Base,
					((LocalMemDesc.RWXAttributes & STM_RSC_MEM_R) != 0) ? 0 : 1,
					((LocalMemDesc.RWXAttributes & STM_RSC_MEM_W) != 0) ? 0 : 1,
					((LocalMemDesc.RWXAttributes & STM_RSC_MEM_X) != 0) ? 0 : 1,
					EptPageAttributeSet,
					-1
					) != 0)
				{
					DEBUG((EFI_D_ERROR, "%ld SmmEPTViolationHandler - STM ERROR unable to add resource to EPT map\n", Index));
					CpuDeadLoop ();
				}
            }
            
            // Check PCIE MMIO.
            if ((mHostContextCommon.PciExpressBaseAddress != 0) &&
                (Address >= mHostContextCommon.PciExpressBaseAddress) &&
                (Address < (mHostContextCommon.PciExpressBaseAddress + mHostContextCommon.PciExpressLength))) {
                PciExpressAddress = Address - mHostContextCommon.PciExpressBaseAddress;
                PciCfgDesc = GetStmResourcePci (
                                                mHostContextCommon.MleProtectedResource.Base,
                                                BUS_FROM_PCIE_ADDRESS(PciExpressAddress),
                                                DEVICE_FROM_PCIE_ADDRESS(PciExpressAddress),
                                                FUNCTION_FROM_PCIE_ADDRESS(PciExpressAddress),
                                                REGISTER_FROM_PCIE_ADDRESS(PciExpressAddress),
                                                (UINT8)(Qualification.UintN & 0x3)
                                                );
                if (PciCfgDesc != NULL) {
                    DEBUG ((EFI_D_ERROR, "%ld EPT (PCIE) violation!\n", Index));
                    AddEventLogForResource (EvtHandledProtectionException, (STM_RSC *)PciCfgDesc);
                    SmmExceptionHandler (Index);
                    CpuDeadLoop ();
                }
                
                PciCfgDesc = GetStmResourcePci (
                                                (STM_RSC *)(UINTN)mGuestContextCommonSmm[VmType].BiosHwResourceRequirementsPtr,
                                                BUS_FROM_PCIE_ADDRESS(PciExpressAddress),
                                                DEVICE_FROM_PCIE_ADDRESS(PciExpressAddress),
                                                FUNCTION_FROM_PCIE_ADDRESS(PciExpressAddress),
                                                REGISTER_FROM_PCIE_ADDRESS(PciExpressAddress),
                                                (UINT8)(Qualification.UintN & 0x3)
                                                );
                if (PciCfgDesc == NULL) {
                    LocalPciCfgDescPtr = (STM_RSC_PCI_CFG_DESC *)LocalPciCfgDescBuf;
                    ZeroMem (LocalPciCfgDescBuf, sizeof(LocalPciCfgDescBuf));
                    LocalPciCfgDescPtr->Hdr.RscType = PCI_CFG_RANGE;
                    LocalPciCfgDescPtr->Hdr.Length = sizeof(STM_RSC_PCI_CFG_DESC); // BUGBUG: Just report this PCI device, it is hard to create PCI hierachy here.
                    LocalPciCfgDescPtr->RWAttributes = (UINT8)(Qualification.UintN & 0x3);
                    LocalPciCfgDescPtr->Base = REGISTER_FROM_PCIE_ADDRESS(PciExpressAddress);
                    LocalPciCfgDescPtr->Length = 1;
                    LocalPciCfgDescPtr->OriginatingBusNumber = BUS_FROM_PCIE_ADDRESS(PciExpressAddress);
                    LocalPciCfgDescPtr->LastNodeIndex = 0;
                    LocalPciCfgDescPtr->PciDevicePath[0].Type = 1;
                    LocalPciCfgDescPtr->PciDevicePath[0].Subtype = 1;
                    LocalPciCfgDescPtr->PciDevicePath[0].Length = sizeof(STM_PCI_DEVICE_PATH_NODE);
                    LocalPciCfgDescPtr->PciDevicePath[0].PciFunction = FUNCTION_FROM_PCIE_ADDRESS(PciExpressAddress);
                    LocalPciCfgDescPtr->PciDevicePath[0].PciDevice = DEVICE_FROM_PCIE_ADDRESS(PciExpressAddress);
          AddEventLogForResource (EvtBiosAccessToUnclaimedResource, (STM_RSC *)LocalPciCfgDescPtr);
                }
                
            }
        }
    }
    
    VmWriteN (VMCS_N_GUEST_RIP_INDEX, VmReadN(VMCS_N_GUEST_RIP_INDEX) + VmRead32(VMCS_32_RO_VMEXIT_INSTRUCTION_LENGTH_INDEX));
    
    return ;
}

/**
 
 This function is EPT misconfiguration handler for SMM.
 
 @param Index CPU index
 
 **/
VOID
SmmEPTMisconfigurationHandler (
                               IN UINT32  Index
                               )
{
    //
    // Should not happen
    //
    DEBUG ((EFI_D_ERROR, "%ld !!!EPTMisconfigurationHandler!!!\n", Index));
    DumpVmcsAllField (Index);
    
    CpuDeadLoop ();
    
    return ;
}

/**
 
 This function is INVEPT handler for SMM.
 
 @param Index CPU index
 
 **/
VOID
SmmInvEPTHandler (
                  IN UINT32  Index
                  )
{
    DEBUG ((EFI_D_ERROR, "%ld !!!InvEPTHandler!!!\n", Index));
    DumpVmcsAllField (Index);
    
    CpuDeadLoop ();
    
    return ;
}

/**
 
 This function sync Ia32PAE page table for EPT.
 
 @param Index CPU index
 
 **/
VOID
Ia32PAESync (
             IN UINT32  Index
             )
{
    UINTN              Cr0;
    UINTN              Cr3;
    UINTN              Cr4;
    UINT32			   VmType;
    
    DEBUG ((EFI_D_INFO, "%ld Ia32PAESync\n", Index));
	  
    VmType = mHostContextCommon.HostContextPerCpu[Index].GuestVmType;  // any VmType other than SMI_HANDLER is a PeVm

    if(SMI_HANDLER != VmType)
	    Index = 0;      // PE VM index is always 0
    //
    // If EPT is enabled and Guest is in IA32 PAE Mode, we need to write PDPTR.
    //
    Cr0 = VmReadN (VMCS_N_GUEST_CR0_INDEX);
    Cr3 = VmReadN (VMCS_N_GUEST_CR3_INDEX);
    Cr4 = VmReadN (VMCS_N_GUEST_CR4_INDEX);
    if (((Cr4 & CR4_PAE) != 0) &&
        ((Cr0 & CR0_PG) != 0) &&
        ((mGuestContextCommonSmm[VmType].GuestContextPerCpu[Index].Efer & IA32_EFER_MSR_MLA) == 0)) {
        VmWrite64 (VMCS_64_GUEST_PDPTE0_INDEX, *(UINT64 *)(Cr3 + sizeof(UINT64) * 0));
        VmWrite64 (VMCS_64_GUEST_PDPTE1_INDEX, *(UINT64 *)(Cr3 + sizeof(UINT64) * 1));
        VmWrite64 (VMCS_64_GUEST_PDPTE2_INDEX, *(UINT64 *)(Cr3 + sizeof(UINT64) * 2));
        VmWrite64 (VMCS_64_GUEST_PDPTE3_INDEX, *(UINT64 *)(Cr3 + sizeof(UINT64) * 3));
    }
    
    return ;
}
