/*
 * Copyright (c) 2018, Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <Debug.h>
#include <Types.h>
#include "AcpiParsing.h"
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <NvmWorkarounds.h>
#include <PlatformConfigData.h>
#include <ShowAcpi.h>
#include <NvmDimmDriver.h>

extern NVMDIMMDRIVER_DATA *gNvmDimmData;

GUID gSpaRangeVolatileRegionGuid = SPA_RANGE_VOLATILE_REGION_GUID;

GUID gSpaRangePmRegionGuid = SPA_RANGE_PM_REGION_GUID;

GUID gSpaRangeControlRegionGuid = SPA_RANGE_CONTROL_REGION_GUID;

GUID gSpaRangeBlockDataWindowRegionGuid = SPA_RANGE_BLOCK_DATA_WINDOW_REGION_GUID;

GUID gSpaRangeRawVolatileRegionGuid = SPA_RANGE_RAW_VOLATILE;

GUID gSpaRangeIsoVolatileRegionGuid = SPA_RANGE_ISO_VOLATILE;

GUID gSpaRangeRawPmRegionGuid = SPA_RANGE_RAW_PM;

GUID gSpaRangeIsoPmRegionGuid = SPA_RANGE_ISO_PM;

GUID gAppDirectPmTypeGuid = APPDIRECT_PM_TYPE;

GUID gSpaRangeMailboxCustomGuid = SPA_RANGE_MAILBOX_CUSTOM_GUID;

GUID gDieTypeDeviceGuid = PMTT_TYPE_DIE_GUID;

GUID gChannelTypeDeviceGuid = PMTT_TYPE_CHANNEL_GUID;

GUID gSlotTypeDeviceGuid = PMTT_TYPE_SLOT_GUID;

/**
  CopyMemoryAndAddPointerToArray - Copies the data and adds the result pointer to an array of pointers.

  @param[in, out] ppTable pointer to the pointers. Warning! This pointer will be freed.
  @param[in] pToAdd pointer to the data that the caller wants to add to the array.
  @param[in] DataSize size of the data that are supposed to be copied.
  @param[in] NewPointerIndex index in the table that the new pointer should have.

  @retval NULL - if a memory allocation failed.
  @retval pointer to the new array of pointers (with the new one at the end).
**/
STATIC
VOID **
CopyMemoryAndAddPointerToArray(
  IN OUT VOID **ppTable,
  IN     VOID *pToAdd,
  IN     UINT32 DataSize,
  IN     UINT32 *pNewPointerIndex
  );

/**
  ParseNfitTable - Performs deserialization from binary memory block into parsed structure of pointers.

  @param[in] pTable pointer to the memory containing the NFIT binary representation.
  @param[out] ppParsedNfit Pointer to a pointer where the allocated and parsed NFIT table will be stored

  @retval EFI_INVALID_PARAMETER One of the provided parameters is invalid
  @retval EFI_VOLUME_CORRUPTED If the table checksum is invalid
  @retval EFI_INCOMPATIBLE_VERSION If the table is not compatible with this ipmctl version
  @retval EFI_SUCCESS
**/
EFI_STATUS
ParseNfitTable(
  IN     VOID *pTable,
     OUT ParsedFitHeader **ppParsedNfit
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  ParsedFitHeader *pParsedNfit = NULL;
  NFitHeader *pNFit = NULL;
  UINT8 *pTabPointer = NULL;
  SubTableHeader *pTableHeader = NULL;
  UINT32 RemainingNFITBytes = 0;

  NVDIMM_ENTRY();

  CHECK_NULL_ARG(pTable, Finish);
  CHECK_NULL_ARG(ppParsedNfit, Finish);

  pNFit = (NFitHeader *)pTable;

  if (!IsChecksumValid(pNFit, pNFit->Header.Length)) {
    NVDIMM_DBG("The checksum of the NFIT table is invalid.");
    ReturnCode = EFI_VOLUME_CORRUPTED;
    goto Finish;
  }

  if (IS_NFIT_REVISION_INVALID(pNFit->Header.Revision)) {
    NVDIMM_DBG("NFIT table revision is invalid");
    ReturnCode = EFI_INCOMPATIBLE_VERSION;
    goto Finish;
  }

  pTabPointer = (UINT8 *)pTable + sizeof(NFitHeader);
  pTableHeader = (SubTableHeader *)pTabPointer;
  RemainingNFITBytes = pNFit->Header.Length - sizeof(*pNFit);
  CHECK_RESULT_MALLOC(*ppParsedNfit, (ParsedFitHeader *)AllocateZeroPool(sizeof(**ppParsedNfit)), Finish);
  pParsedNfit = *ppParsedNfit;

  CHECK_RESULT_MALLOC(pParsedNfit->pFit, (NFitHeader *)AllocateZeroPool(sizeof(*(pParsedNfit->pFit))), Finish);

  CopyMem_S(pParsedNfit->pFit, sizeof(*(pParsedNfit->pFit)), pNFit, sizeof(*(pParsedNfit->pFit)));

  while (RemainingNFITBytes > 0) {
    if (pTableHeader->Length == 0) {
      NVDIMM_DBG("Zero size entry found in nfit region.");
      goto Finish;
    }

    RemainingNFITBytes -= pTableHeader->Length;

    switch(pTableHeader->Type) {
    case NVDIMM_SPA_RANGE_TYPE:
      CHECK_RESULT_MALLOC(pParsedNfit->ppSpaRangeTbles, (SpaRangeTbl **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedNfit->ppSpaRangeTbles, pTabPointer,
          pTableHeader->Length, &pParsedNfit->SpaRangeTblesNum), Finish);
      break;
    case NVDIMM_NVDIMM_REGION_TYPE:
      CHECK_RESULT_MALLOC(pParsedNfit->ppNvDimmRegionMappingStructures, (NvDimmRegionMappingStructure **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedNfit->ppNvDimmRegionMappingStructures, pTabPointer,
          pTableHeader->Length, &pParsedNfit->NvDimmRegionMappingStructuresNum), Finish);
      break;
    case NVDIMM_INTERLEAVE_TYPE:
      CHECK_RESULT_MALLOC(pParsedNfit->ppInterleaveTbles, (InterleaveStruct **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedNfit->ppInterleaveTbles, pTabPointer,
          pTableHeader->Length, &pParsedNfit->InterleaveTblesNum), Finish);
      break;
    case NVDIMM_SMBIOS_MGMT_INFO_TYPE:
      CHECK_RESULT_MALLOC(pParsedNfit->ppSmbiosTbles, (SmbiosTbl **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedNfit->ppSmbiosTbles, pTabPointer,
          pTableHeader->Length, &pParsedNfit->SmbiosTblesNum), Finish);
      break;
    case NVDIMM_CONTROL_REGION_TYPE:
      CHECK_RESULT_MALLOC(pParsedNfit->ppControlRegionTbles, (ControlRegionTbl **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedNfit->ppControlRegionTbles, pTabPointer,
          pTableHeader->Length, &pParsedNfit->ControlRegionTblesNum), Finish);
      break;
    case NVDIMM_BW_DATA_WINDOW_REGION_TYPE:
      CHECK_RESULT_MALLOC(pParsedNfit->ppBWRegionTbles, (BWRegionTbl **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedNfit->ppBWRegionTbles, pTabPointer,
          pTableHeader->Length, &pParsedNfit->BWRegionTblesNum), Finish);
      break;
    case NVDIMM_FLUSH_HINT_TYPE:
      CHECK_RESULT_MALLOC(pParsedNfit->ppFlushHintTbles, (FlushHintTbl **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedNfit->ppFlushHintTbles, pTabPointer,
          pTableHeader->Length, &pParsedNfit->FlushHintTblesNum), Finish);
      break;
    case NVDIMM_PLATFORM_CAPABILITIES_TYPE:
      CHECK_RESULT_MALLOC(pParsedNfit->ppPlatformCapabilitiesTbles, (PlatformCapabilitiesTbl **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedNfit->ppPlatformCapabilitiesTbles, pTabPointer,
          pTableHeader->Length, &pParsedNfit->PlatformCapabilitiesTblesNum), Finish);
      break;
    default:
      break;
    }

    pTabPointer += pTableHeader->Length;
    pTableHeader = (SubTableHeader *)pTabPointer;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  if (EFI_ERROR(ReturnCode) && ppParsedNfit != NULL && *ppParsedNfit != NULL) {
    FreeParsedNfit(ppParsedNfit);
  }
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Performs deserialization from binary memory block, containing PCAT tables, into parsed structure of pointers.

  @param[in] pTable pointer to the memory containing the PCAT binary representation.
  @param[out] ppParsedPcat Pointer to a pointer where the allocated and parsed PCAT table will be stored

  @retval EFI_INVALID_PARAMETER One of the provided parameters is invalid
  @retval EFI_VOLUME_CORRUPTED If the table checksum is invalid
  @retval EFI_INCOMPATIBLE_VERSION If the table is not compatible with this ipmctl version
  @retval EFI_SUCCESS
**/
EFI_STATUS
ParsePcatTable (
  IN     VOID *pTable,
     OUT ParsedPcatHeader **ppParsedPcat
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  ParsedPcatHeader *pParsedPcat = NULL;                 //!< Output Parsed PCAT structures
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPcatHeader = NULL; //!< PCAT header
  PCAT_TABLE_HEADER *pPcatSubTableHeader = NULL;        //!< PCAT subtable header
  UINT32 RemainingPcatBytes = 0;
  UINT32 Length = 0;

  NVDIMM_ENTRY();

  CHECK_NULL_ARG(pTable, Finish);
  CHECK_NULL_ARG(ppParsedPcat, Finish);

  pPcatHeader = (PLATFORM_CONFIG_ATTRIBUTES_TABLE *) pTable;

  if (!IsChecksumValid(pPcatHeader, pPcatHeader->Header.Length)) {
    NVDIMM_DBG("The checksum of PCAT table is invalid.");
    ReturnCode = EFI_VOLUME_CORRUPTED;
    goto Finish;
  }

  if (IS_PCAT_REVISION_INVALID(pPcatHeader->Header.Revision)) {
    NVDIMM_DBG("PCAT table revision is invalid");
    ReturnCode = EFI_INCOMPATIBLE_VERSION;
    goto Finish;
  }

  pPcatSubTableHeader = (PCAT_TABLE_HEADER *) &pPcatHeader->pPcatTables;

  RemainingPcatBytes = pPcatHeader->Header.Length - sizeof(*pPcatHeader);

  CHECK_RESULT_MALLOC(*ppParsedPcat, (ParsedPcatHeader *)AllocateZeroPool(sizeof(*pParsedPcat)), Finish);
  pParsedPcat = *ppParsedPcat;

  CHECK_RESULT_MALLOC(pParsedPcat->pPlatformConfigAttr, (PLATFORM_CONFIG_ATTRIBUTES_TABLE *)AllocateZeroPool(
      sizeof(*pParsedPcat->pPlatformConfigAttr)), Finish);

  // Copying PCAT header to parsed structure
  CopyMem_S(pParsedPcat->pPlatformConfigAttr, sizeof(*pParsedPcat->pPlatformConfigAttr), pPcatHeader, sizeof(*pParsedPcat->pPlatformConfigAttr));

  // Looking for sub tables
  while (RemainingPcatBytes > 0) {
    Length = pPcatSubTableHeader->Length;

    if (Length == 0) {
      NVDIMM_DBG("Length can't be 0.");
      goto Finish;
    }

    switch(pPcatSubTableHeader->Type) {
    case PCAT_TYPE_PLATFORM_CAPABILITY_INFO_TABLE:
      if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPcatHeader)) {
        CHECK_RESULT_MALLOC(pParsedPcat->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo, (PLATFORM_CAPABILITY_INFO **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPcat->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo, pPcatSubTableHeader, Length,
          &pParsedPcat->PlatformCapabilityInfoNum), Finish);
      }
      else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPcatHeader)) {
        CHECK_RESULT_MALLOC(pParsedPcat->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo, (PLATFORM_CAPABILITY_INFO3 **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPcat->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo, pPcatSubTableHeader, Length,
          &pParsedPcat->PlatformCapabilityInfoNum), Finish);

        if (IS_ACPI_HEADER_REV_MAJ_1_MIN_1_OR_2(pPcatHeader)) {
          // Backwards compatability. Platforms with PCAT revison < 1.3 always support mixed mode
          pParsedPcat->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[pParsedPcat->PlatformCapabilityInfoNum - 1]->
            MemoryModeCapabilities.MemoryModesFlags.MixedMode = MIXED_MODE_CAPABILITY_SUPPORTED;
        }
      }
      break;

    case PCAT_TYPE_INTERLEAVE_CAPABILITY_INFO_TABLE:
      if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPcatHeader)) {
        CHECK_RESULT_MALLOC(pParsedPcat->pPcatVersion.Pcat2Tables.ppMemoryInterleaveCapabilityInfo, (MEMORY_INTERLEAVE_CAPABILITY_INFO **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPcat->pPcatVersion.Pcat2Tables.ppMemoryInterleaveCapabilityInfo, pPcatSubTableHeader, Length,
          &pParsedPcat->MemoryInterleaveCapabilityInfoNum), Finish);
      }
      else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPcatHeader)) {
        CHECK_RESULT_MALLOC(pParsedPcat->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo, (MEMORY_INTERLEAVE_CAPABILITY_INFO3 **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPcat->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo, pPcatSubTableHeader, Length,
          &pParsedPcat->MemoryInterleaveCapabilityInfoNum), Finish);
      }
      break;

    case PCAT_TYPE_RUNTIME_INTERFACE_TABLE:
      CHECK_RESULT_MALLOC(pParsedPcat->ppRuntimeInterfaceValConfInput, (RECONFIGURATION_INPUT_VALIDATION_INTERFACE_TABLE **) CopyMemoryAndAddPointerToArray(
          (VOID **) pParsedPcat->ppRuntimeInterfaceValConfInput, pPcatSubTableHeader, Length,
          &pParsedPcat->RuntimeInterfaceValConfInputNum), Finish);
      break;

    case PCAT_TYPE_CONFIG_MANAGEMENT_ATTRIBUTES_TABLE:
      CHECK_RESULT_MALLOC(pParsedPcat->ppConfigManagementAttributesInfo, (CONFIG_MANAGEMENT_ATTRIBUTES_EXTENSION_TABLE **) CopyMemoryAndAddPointerToArray(
          (VOID **) pParsedPcat->ppConfigManagementAttributesInfo, pPcatSubTableHeader, Length,
          &pParsedPcat->ConfigManagementAttributesInfoNum), Finish);
      break;

    case PCAT_TYPE_SOCKET_SKU_INFO_TABLE:
      if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPcatHeader)) {
        CHECK_RESULT_MALLOC(pParsedPcat->pPcatVersion.Pcat2Tables.ppSocketSkuInfoTable, (SOCKET_SKU_INFO_TABLE **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPcat->pPcatVersion.Pcat2Tables.ppSocketSkuInfoTable, pPcatSubTableHeader, Length,
          &pParsedPcat->SocketSkuInfoNum), Finish);
      }
      else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPcatHeader)) {
        CHECK_RESULT_MALLOC(pParsedPcat->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable, (DIE_SKU_INFO_TABLE **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPcat->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable, pPcatSubTableHeader, Length,
          &pParsedPcat->SocketSkuInfoNum), Finish);
      }
      break;

    default:
      NVDIMM_WARN("Unknown type of PCAT table.");
      goto Finish;
    }

    RemainingPcatBytes -= Length;
    pPcatSubTableHeader = (PCAT_TABLE_HEADER *) ((UINT8 *)pPcatSubTableHeader + Length);
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  if (EFI_ERROR(ReturnCode) && NULL != ppParsedPcat && NULL != *ppParsedPcat) {
    FreeParsedPcat(ppParsedPcat);
  }

  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Performs deserialization from binary memory block, containing PMTT tables, into parsed structure of pointers.

  @param[in] pTable pointer to the memory containing the PMTT binary representation.
  @param[out] ppParsedPmtt Pointer to a pointer where the allocated and parsed PMTT table will be stored

  @retval EFI_INVALID_PARAMETER One of the provided parameters is invalid
  @retval EFI_VOLUME_CORRUPTED If the table checksum is invalid
  @retval EFI_INCOMPATIBLE_VERSION If the table is not compatible with this ipmctl version
  @retval EFI_SUCCESS
**/
EFI_STATUS
ParsePmttTable(
  IN     VOID *pTable,
     OUT ParsedPmttHeader **ppParsedPmtt
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  ParsedPmttHeader *pParsedPmtt = NULL;                 //!< Output Parsed PMTT structures
  PMTT_TABLE2 *pPmttHeader = NULL; //!< PMTT header
  PMTT_COMMON_HEADER2 *pPmttCommonTableHeader = NULL;        //!< PMTT common header
  PMTT_MODULE_INFO *pModuleInfo = NULL;
  UINT32 RemainingPmttBytes = 0;
  UINT32 Length = 0;
  UINT16 SocketID = 0;
  UINT16 DieID = 0;
  UINT16 CpuID = 0;
  UINT16 iMCID = 0;
  UINT16 ChannelID = 0;
  UINT16 SlotID = 0;
  UINT32 NumOfMemoryDevices = 0;
  UINT32 DieLevelNumOfMemoryDevices = 0;

  NVDIMM_ENTRY();

  CHECK_NULL_ARG(pTable, Finish);
  CHECK_NULL_ARG(ppParsedPmtt, Finish);
  // Prepare for error scenario first
  *ppParsedPmtt = NULL;

  pPmttHeader = (PMTT_TABLE2 *)pTable;

  if (!IsChecksumValid(pPmttHeader, pPmttHeader->Header.Length)) {
    NVDIMM_DBG("The checksum of PMTT table is invalid.");
    ReturnCode = EFI_VOLUME_CORRUPTED;
    goto Finish;
  }

  /**
    Parse the PMTT Rev 0.2 table only
    ACPI 6.3 requires DIMM fields to be populated using PMTT
    if NfitDeviceHandle Bit 31 is set
  **/
  if (IS_PMTT_REVISION_INVALID(pPmttHeader->Header.Revision)) {
    // PMTT != 0.1 and PMTT != 0.2
    NVDIMM_DBG("PMTT table revision is invalid");
    ReturnCode = EFI_INCOMPATIBLE_VERSION;
    goto Finish;
  } else if (IS_ACPI_REV_MAJ_0_MIN_1(pPmttHeader->Header.Revision)) {
    NVDIMM_DBG("Choosing to not parse PMTT table right now, will parse later as needed");
    ReturnCode = EFI_SUCCESS;
    goto Finish;
  }

  // PMTT == 0.2

  pPmttCommonTableHeader = (PMTT_COMMON_HEADER2 *)&pPmttHeader->pPmttDevices;
  RemainingPmttBytes = pPmttHeader->Header.Length - sizeof(*pPmttHeader);

  CHECK_RESULT_MALLOC(*ppParsedPmtt, (ParsedPmttHeader *)AllocateZeroPool(sizeof(*pParsedPmtt)), Finish);
  pParsedPmtt = *ppParsedPmtt;

  CHECK_RESULT_MALLOC(pParsedPmtt->pPmtt, (PMTT_TABLE2 *)AllocateZeroPool(sizeof(*pParsedPmtt->pPmtt)), Finish);

  // Copying PMTT header to parsed structure
  CopyMem_S(pParsedPmtt->pPmtt, sizeof(*pParsedPmtt->pPmtt), pPmttHeader, sizeof(*pParsedPmtt->pPmtt));

  // Looking for sub tables
  while (RemainingPmttBytes > 0) {
    Length = pPmttCommonTableHeader->Length;
    if (Length == 0) {
      NVDIMM_DBG("Length of PMTT common header is zero.");
      goto Finish;
    }

    if (!(pPmttCommonTableHeader->Flags & PMTT_PHYSICAL_ELEMENT_OF_TOPOLOGY)) {
      NVDIMM_DBG("Not a physical element of the topology!");
      RemainingPmttBytes -= Length;
      pPmttCommonTableHeader = (PMTT_COMMON_HEADER2 *)((UINT8 *)pPmttCommonTableHeader + Length);
      continue;
    }

    switch (pPmttCommonTableHeader->Type) {
    case PMTT_TYPE_SOCKET:
    {
      CHECK_RESULT_MALLOC(pParsedPmtt->ppSockets, (PMTT_SOCKET2 **)CopyMemoryAndAddPointerToArray(
        (VOID **)pParsedPmtt->ppSockets, pPmttCommonTableHeader, Length,
        &pParsedPmtt->SocketsNum), Finish);
      SocketID = pParsedPmtt->ppSockets[pParsedPmtt->SocketsNum - 1]->SocketId;
      DieLevelNumOfMemoryDevices += NumOfMemoryDevices;
      NumOfMemoryDevices = pParsedPmtt->ppSockets[pParsedPmtt->SocketsNum - 1]->Header.NoOfMemoryDevices;
      DieID = MAX_DIEID_SINGLE_DIE_SOCKET;
      break;
    }

    case PMTT_TYPE_VENDOR_SPECIFIC:
    {
      PMTT_VENDOR_SPECIFIC2 *pVendorDevice = (PMTT_VENDOR_SPECIFIC2 *)((UINT8 *)pPmttCommonTableHeader);
      if (CompareMem(&pVendorDevice->TypeUUID, &gDieTypeDeviceGuid, sizeof(pVendorDevice->TypeUUID)) == 0) {
        CHECK_RESULT_MALLOC(pParsedPmtt->ppDies, (PMTT_VENDOR_SPECIFIC2 **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPmtt->ppDies, pPmttCommonTableHeader, Length,
          &pParsedPmtt->DiesNum), Finish);
        DieID = pParsedPmtt->ppDies[pParsedPmtt->DiesNum - 1]->DeviceID;
        CpuID = (DieLevelNumOfMemoryDevices & MAX_UINT16) + DieID;
      }
      else if (CompareMem(&pVendorDevice->TypeUUID, &gChannelTypeDeviceGuid, sizeof(pVendorDevice->TypeUUID)) == 0) {
        CHECK_RESULT_MALLOC(pParsedPmtt->ppChannels, (PMTT_VENDOR_SPECIFIC2 **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPmtt->ppChannels, pPmttCommonTableHeader, Length,
          &pParsedPmtt->ChannelsNum), Finish);
        ChannelID = pParsedPmtt->ppChannels[pParsedPmtt->ChannelsNum - 1]->DeviceID;
        SlotID = 0;
      }
      else if (CompareMem(&pVendorDevice->TypeUUID, &gSlotTypeDeviceGuid, sizeof(pVendorDevice->TypeUUID)) == 0) {
        CHECK_RESULT_MALLOC(pParsedPmtt->ppSlots, (PMTT_VENDOR_SPECIFIC2 **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPmtt->ppSlots, pPmttCommonTableHeader, Length,
          &pParsedPmtt->SlotsNum), Finish);
        SlotID = pParsedPmtt->ppSlots[pParsedPmtt->SlotsNum - 1]->DeviceID;
      }
      else {
        NVDIMM_DBG("Unknown PMTT Vendor Specific Data");
        continue;
      }
      break;
    }

    case PMTT_TYPE_iMC:
      CHECK_RESULT_MALLOC(pParsedPmtt->ppiMCs, (PMTT_iMC2 **)CopyMemoryAndAddPointerToArray(
        (VOID **)pParsedPmtt->ppiMCs, pPmttCommonTableHeader, Length,
        &pParsedPmtt->iMCsNum), Finish);
      iMCID = pParsedPmtt->ppiMCs[pParsedPmtt->iMCsNum - 1]->MemControllerID;
      ChannelID = 0;
      break;

    case PMTT_TYPE_MODULE:
    {
      PMTT_MODULE2 *pModule = (PMTT_MODULE2 *)pPmttCommonTableHeader;

      // skip if Bits [3:2] are reserved
      if ((pPmttCommonTableHeader->Flags & PMTT_TYPE_RESERVED) == PMTT_TYPE_RESERVED) {
        NVDIMM_DBG("Reserved. No indication in PMTT if this module is volatile or non-volatile memory!");
        break;
      }

      pModuleInfo = (PMTT_MODULE_INFO *)AllocateZeroPool(sizeof(*pModuleInfo));
      if (pModuleInfo == NULL) {
        NVDIMM_DBG("Memory allocation error.");
        goto Finish;
      }
      pModuleInfo->Header = pModule->Header;
      pModuleInfo->SmbiosHandle = pModule->SmbiosHandle & SMBIOS_HANDLE_MASK;
      pModuleInfo->SocketId = SocketID;
      pModuleInfo->DieId = DieID;
      pModuleInfo->CpuId = CpuID;
      pModuleInfo->MemControllerId = iMCID;
      pModuleInfo->ChannelId = ChannelID;
      pModuleInfo->SlotId = SlotID;

      // BIT 2 is set then DCPMM or else DDR type
      if (pPmttCommonTableHeader->Flags & PMTT_DDR_DCPM_FLAG) {
        pModuleInfo->MemoryType = MEMORYTYPE_DCPM;
        CHECK_RESULT_MALLOC(pParsedPmtt->ppDCPMModules, (PMTT_MODULE_INFO **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPmtt->ppDCPMModules, pModuleInfo, sizeof(*pModuleInfo),
          &pParsedPmtt->DCPMModulesNum), Finish);
      }
      else {
        pModuleInfo->MemoryType = MEMORYTYPE_DDR4;
        CHECK_RESULT_MALLOC(pParsedPmtt->ppDDRModules, (PMTT_MODULE_INFO **)CopyMemoryAndAddPointerToArray(
          (VOID **)pParsedPmtt->ppDDRModules, pModuleInfo, sizeof(*pModuleInfo),
          &pParsedPmtt->DDRModulesNum), Finish);
      }
      FREE_POOL_SAFE(pModuleInfo);
      break;
    }

    default:
      NVDIMM_WARN("Unknown type of PMTT table.");
      goto Finish;
    }


    RemainingPmttBytes -= Length;
    pPmttCommonTableHeader = (PMTT_COMMON_HEADER2 *)((UINT8 *)pPmttCommonTableHeader + Length);
  }


  ReturnCode = EFI_SUCCESS;

Finish:
  if (EFI_ERROR(ReturnCode) && NULL != ppParsedPmtt && NULL != *ppParsedPmtt) {
    FreeParsedPmtt(ppParsedPmtt);
  }

  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Get PMTT Dimm Module by Dimm ID
  Scan the dimm list for a dimm identified by Dimm ID

  @param[in] DimmID: The SMBIOS Type 17 handle of the dimm
  @param[in] pPmttHead: Parsed PMTT Table

  @retval PMTT_MODULE_INFO struct pointer if matching dimm has been found
  @retval NULL pointer if not found
**/
PMTT_MODULE_INFO *
GetDimmModuleByPidFromPmtt(
  IN     UINT32 DimmID,
  IN     ParsedPmttHeader *pPmttHead
  )
{
  UINT32 Index = 0;
  PMTT_MODULE_INFO *pModuleInfo = NULL;
  NVDIMM_ENTRY();

  if (pPmttHead == NULL) {
    NVDIMM_DBG("PMTT Table header NULL");
    goto Finish;
  }

  for (Index = 0; Index < pPmttHead->DCPMModulesNum; Index++) {
    if (pPmttHead->ppDCPMModules[Index]->SmbiosHandle == DimmID) {
      pModuleInfo = pPmttHead->ppDCPMModules[Index];
    }
  }

  Finish:
   NVDIMM_EXIT();
   return pModuleInfo;
}

/**
  Retrieve the Logical Socket ID from PMTT Table

  @param[in] SocketId SocketID
  @param[in] DieId DieID
  @param[out] pLogicalSocketId Logical socket ID based on Dimm socket ID & Die ID

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameter is NULL
  @retval EFI_NOT_FOUND PCAT socket sku info table not found for given socketID
**/
EFI_STATUS
GetLogicalSocketIdFromPmtt(
  IN     UINT32 SocketId,
  IN     UINT32 DieId,
  OUT    UINT32 *pLogicalSocketId
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  TABLE_HEADER *pTable = NULL;
  UINT32 Index = 0;
  UINT32 NoOfMemoryDevices = 0;
  BOOLEAN Found = FALSE;
  ParsedPmttHeader *pPmttHead = NULL;

  NVDIMM_ENTRY();

  if (pLogicalSocketId == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  // Only PMTT >= 0.2 are parsed. Anything else is NULL
  pPmttHead = gNvmDimmData->PMEMDev.pPmttHead;

  if (pPmttHead == NULL) {
    // If the parsed PMTT table is missing, we are on a Purley platform and the logical
    // socket is the same as a physical socket. No changes needed
    *pLogicalSocketId = SocketId;
    ReturnCode = EFI_SUCCESS;
    goto Finish;
  }

  for (Index = 0; Index < pPmttHead->SocketsNum; Index++) {
    if (SocketId == pPmttHead->ppSockets[Index]->SocketId) {
      Found = TRUE;
      break;
    }
    NoOfMemoryDevices += pPmttHead->ppSockets[Index]->Header.NoOfMemoryDevices;
  }

  if (!Found) {
    NVDIMM_DBG("Socket ID not found");
    ReturnCode = EFI_NOT_FOUND;
    goto Finish;
  }

  // Searching for matching Die ID
  // Reset Found to FALSE again (from TRUE)
  Found = FALSE;
  for (Index = 0; Index < pPmttHead->DiesNum; Index++) {
    if (DieId == pPmttHead->ppDies[Index]->DeviceID) {
      *pLogicalSocketId = NoOfMemoryDevices + DieId;
      Found = TRUE;
      break;
    }
  }

  if (!Found) {
    NVDIMM_DBG("Die ID not found");
    ReturnCode = EFI_NOT_FOUND;
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

  Finish:
    FREE_POOL_SAFE(pTable);
    NVDIMM_EXIT_I64(ReturnCode);
    return ReturnCode;
}

/**
  Performs deserialization from binary memory block containing PMTT table and checks if memory mode can be configured.

  @param[in] pTable pointer to the memory containing the PMTT binary representation.

  @retval false if topology does NOT allows MM.
  @retval true if topology allows MM.
**/
BOOLEAN
CheckIsMemoryModeAllowed(
  IN     TABLE_HEADER *pTable
  )
{
  BOOLEAN MMCanBeConfigured = FALSE;
  BOOLEAN IsDDR = FALSE;
  BOOLEAN IsDCPM = FALSE;

  if (pTable == NULL) {
    goto Finish;
  }
  if (!IsChecksumValid(pTable, pTable->Length)) {
    NVDIMM_WARN("The checksum of PMTT table is invalid.");
    goto Finish;
  }

  if (IS_ACPI_REV_MAJ_0_MIN_1(pTable->Revision)) {
    PMTT_TABLE *pPMTT = (PMTT_TABLE *)pTable;
    UINT64 Offset = sizeof(pPMTT->Header) + sizeof(pPMTT->Reserved);
    PMTT_COMMON_HEADER *pCommonHeader = (PMTT_COMMON_HEADER *)(((UINT8 *)pPMTT) + Offset);

    while (Offset < pPMTT->Header.Length && pCommonHeader->Type == PMTT_TYPE_SOCKET) {
      // check if socket is enabled
      if (pCommonHeader->Flags) {
        Offset += sizeof(PMTT_SOCKET) + PMTT_COMMON_HDR_LEN;
        pCommonHeader = (PMTT_COMMON_HEADER *)(((UINT8 *)pPMTT) + Offset);
        while (Offset < pPMTT->Header.Length && pCommonHeader->Type == PMTT_TYPE_iMC) {
          // check if iMC is enabled
          if (pCommonHeader->Flags) {
            Offset += sizeof(PMTT_iMC) + PMTT_COMMON_HDR_LEN;
            pCommonHeader = (PMTT_COMMON_HEADER *)(((UINT8 *)pPMTT) + Offset);
            // check if at least one DCPMM is present
            while (Offset < pPMTT->Header.Length && pCommonHeader->Type == PMTT_TYPE_MODULE) {
              PMTT_MODULE *pModule = (PMTT_MODULE *)(((UINT8 *)pCommonHeader) + sizeof(pCommonHeader));
              // if IsDCPM is already set then continue to loop to find the offset of the next aggregated device
              if (!IsDCPM) {
                // bit 2 is set then DCPMM
                if ((pCommonHeader->Flags & PMTT_DDR_DCPM_FLAG) && pModule->SizeOfDimm > 0) {
                  IsDCPM = TRUE;
                }
                else if (!(pCommonHeader->Flags & PMTT_DDR_DCPM_FLAG) && pModule->SizeOfDimm > 0) {
                  IsDDR = TRUE;
                }
              }
              Offset += sizeof(PMTT_MODULE) + PMTT_COMMON_HDR_LEN;
              pCommonHeader = (PMTT_COMMON_HEADER *)(((UINT8 *)pPMTT) + Offset);
            } // end of Module
            if (IsDDR && !IsDCPM) {
              MMCanBeConfigured = FALSE;
              goto Finish;
            }
            MMCanBeConfigured = TRUE;
            IsDDR = FALSE;
            IsDCPM = FALSE;
          }
          else {
            // iMC is disabled
            Offset += pCommonHeader->Length;
            pCommonHeader = (PMTT_COMMON_HEADER *)(((UINT8 *)pPMTT) + Offset);
          }
        } // end of iMC
      }
      else { // socket is disabled
        Offset += pCommonHeader->Length;
        pCommonHeader = (PMTT_COMMON_HEADER *)(((UINT8 *)pPMTT) + Offset);
      }
    } // end of socket
  }
  else if (IS_ACPI_REV_MAJ_0_MIN_2(pTable->Revision)) {
    PMTT_TABLE2 *pPMTT = NULL;
    UINT32 Index1 = 0;
    UINT32 Index2 = 0;

    if (gNvmDimmData->PMEMDev.pPmttHead == NULL
      || gNvmDimmData->PMEMDev.pPmttHead->iMCsNum == 0) {
      NVDIMM_DBG("Incorrect PMTT table");
      goto Finish;
    }

    pPMTT = gNvmDimmData->PMEMDev.pPmttHead->pPmtt;
    if (IS_ACPI_HEADER_REV_MAJ_0_MIN_2(pPMTT)) {
      for (Index1 = 0; Index1 < gNvmDimmData->PMEMDev.pPmttHead->DDRModulesNum; Index1++) {
        for (Index2 = 0; Index2 < gNvmDimmData->PMEMDev.pPmttHead->DCPMModulesNum; Index2++) {
          if (gNvmDimmData->PMEMDev.pPmttHead->ppDDRModules[Index1]->SocketId == gNvmDimmData->PMEMDev.pPmttHead->ppDCPMModules[Index2]->SocketId &&
            gNvmDimmData->PMEMDev.pPmttHead->ppDDRModules[Index1]->DieId == gNvmDimmData->PMEMDev.pPmttHead->ppDCPMModules[Index2]->DieId &&
            gNvmDimmData->PMEMDev.pPmttHead->ppDDRModules[Index1]->MemControllerId == gNvmDimmData->PMEMDev.pPmttHead->ppDCPMModules[Index2]->MemControllerId) {
            IsDCPM = TRUE;
            break;
          }
        }

        if (!IsDCPM) {
          MMCanBeConfigured = FALSE;
          goto Finish;
        }
        IsDCPM = FALSE;
      }
      MMCanBeConfigured = TRUE;
    }
  }

Finish:
  return MMCanBeConfigured;
}

/**
  Returns the FlushHint table associated with the provided NVDIMM region table.

  @param[in] pFitHead pointer to the parsed NFit Header structure.
  @param[in] pNvDimmRegionMappingStructure the NVDIMM region table that contains the index.
  @param[out] ppFlushHintTable pointer to a pointer where the table will be stored.

  @retval EFI_SUCCESS if the table was found and is properly returned.
  @retval EFI_INVALID_PARAMETER if one or more parameters equal NULL.
  @retval EFI_NOT_FOUND if there is no Interleave table with the provided index.
**/
EFI_STATUS
GetFlushHintTableForNvDimmRegionTable(
  IN     ParsedFitHeader *pFitHead,
  IN     NvDimmRegionMappingStructure *pNvDimmRegionMappingStructure,
     OUT FlushHintTbl **ppFlushHintTable
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  UINT32 Index = 0;

  if (pFitHead == NULL || pNvDimmRegionMappingStructure == NULL || ppFlushHintTable == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  for (Index = 0; Index < pFitHead->FlushHintTblesNum; Index++) {
    if (pNvDimmRegionMappingStructure->DeviceHandle.AsUint32 == pFitHead->ppFlushHintTbles[Index]->DeviceHandle.AsUint32) {
      *ppFlushHintTable = pFitHead->ppFlushHintTbles[Index];
      ReturnCode = EFI_SUCCESS;
    }
  }

Finish:
  return ReturnCode;
}

/**
  GetBlockDataWindowRegDescTabl - returns the Block Data Window Table associated with the provided Control Region Table.

  @param[in] pFitHead pointer to the parsed NFit Header structure.
  @param[in] pControlRegionTable the Control Region table that contains the index.
  @param[out] ppBlockDataWindowTable pointer to a pointer where the table will be stored.

  @retval EFI_SUCCESS if the table was found and is properly returned.
  @retval EFI_INVALID_PARAMETER if pFitHead or ControlRegionTbl or BWRegionTbl equals NULL.
  @retval EFI_NOT_FOUND if there is no Block Data Window Descriptor table with the provided index.
**/
EFI_STATUS
GetBlockDataWindowRegDescTabl(
  IN     ParsedFitHeader *pFitHead,
  IN     ControlRegionTbl *pControlRegTbl,
     OUT BWRegionTbl **ppBlockDataWindowTable
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  UINT16 Index = 0;
  UINT16 ControlTableIndex = 0;

  if (pFitHead == NULL || pControlRegTbl == NULL || ppBlockDataWindowTable == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  ControlTableIndex = pControlRegTbl->ControlRegionDescriptorTableIndex;

  for (Index = 0; Index < pFitHead->BWRegionTblesNum; Index++) {
    if (pFitHead->ppBWRegionTbles[Index]->ControlRegionStructureIndex == ControlTableIndex) {
      *ppBlockDataWindowTable = pFitHead->ppBWRegionTbles[Index];
      ReturnCode = EFI_SUCCESS;
      break;
    }
  }

Finish:
  return ReturnCode;
}

/**
  Returns the ControlRegion table associated with the provided NVDIMM region table.

  @param[in] pFitHead pointer to the parsed NFit Header structure.
  @param[in] pNvDimmRegionMappingStructure the NVDIMM region table that contains the index.
  @param[out] ppControlRegionTable pointer to a pointer where the table will be stored.

  @retval EFI_SUCCESS if the table was found and is properly returned.
  @retval EFI_INVALID_PARAMETER if one or more input parameters equal NULL.
  @retval EFI_NOT_FOUND if there is no Control Region table with the provided index.
**/
EFI_STATUS
GetControlRegionTableForNvDimmRegionTable(
  IN     ParsedFitHeader *pFitHead,
  IN     NvDimmRegionMappingStructure *pNvDimmRegionMappingStructure,
     OUT ControlRegionTbl **ppControlRegionTable
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  UINT16 Index = 0;
  UINT16 ControlTableIndex = 0;

  if (pFitHead == NULL || pNvDimmRegionMappingStructure == NULL || ppControlRegionTable == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  *ppControlRegionTable = NULL;
  ControlTableIndex = pNvDimmRegionMappingStructure->NvdimmControlRegionDescriptorTableIndex;

  for (Index = 0; Index < pFitHead->ControlRegionTblesNum; Index++) {
    if (pFitHead->ppControlRegionTbles[Index]->ControlRegionDescriptorTableIndex == ControlTableIndex) {
      *ppControlRegionTable = pFitHead->ppControlRegionTbles[Index];
      ReturnCode = EFI_SUCCESS;
      break;
    }
  }

Finish:
  return ReturnCode;
}

/**
  Get Control Region table for provided PhysicalID

  @param[in] pFitHead pointer to the parsed NFit Header structure
  @param[in] Pid Dimm PhysicalID
  @param[out] pControlRegionTables array to store Control Region tables pointers
  @param[in, out] pControlRegionTablesNum size of array on input, number of items stored in the array on output

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER One or more parameters are NULL
  @retval EFI_BUFFER_TOO_SMALL There is more Control Region tables in NFIT than size of provided array
**/
EFI_STATUS
GetControlRegionTablesForPID(
  IN     ParsedFitHeader *pFitHead,
  IN     UINT16 Pid,
     OUT ControlRegionTbl *pControlRegionTables[],
  IN OUT UINT32 *pControlRegionTablesNum
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  UINT32 Index = 0;
  UINT32 Index2 = 0;
  UINT32 CurrentArrayNum = 0;
  ControlRegionTbl *pCtrlTable = NULL;
  BOOLEAN ContainedAlready = FALSE;

  NVDIMM_ENTRY();

  if (pFitHead == NULL || pControlRegionTables == NULL || pControlRegionTablesNum == NULL) {
    goto Finish;
  }

  for (Index = 0; Index < pFitHead->NvDimmRegionMappingStructuresNum; Index++) {
    if (Pid == pFitHead->ppNvDimmRegionMappingStructures[Index]->NvDimmPhysicalId) {
      ReturnCode = GetControlRegionTableForNvDimmRegionTable(
          pFitHead, pFitHead->ppNvDimmRegionMappingStructures[Index], &pCtrlTable);

      /** Make sure the found Control Region table is not in the array already. **/
      ContainedAlready = FALSE;
      for (Index2 = 0; Index2 < CurrentArrayNum; Index2++) {
        if (pCtrlTable == pControlRegionTables[Index2]) {
          ContainedAlready = TRUE;
        }
      }

      if (!ContainedAlready) {
        if (CurrentArrayNum >= *pControlRegionTablesNum) {
          NVDIMM_ERR("There are more Control Region tables than length of the input array.");
          ReturnCode = EFI_BUFFER_TOO_SMALL;
          goto Finish;
        }
        pControlRegionTables[CurrentArrayNum] = pCtrlTable;
        CurrentArrayNum++;
      }
    }
  }

  *pControlRegionTablesNum = CurrentArrayNum;
  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  GetSpaRangeTable - returns the SpaRange Table with the provided Index.

  @param[in] pFitHead pointer to the parsed NFit Header structure.
  @param[in] SpaRangeTblIndex index of the table to be found.
  @param[out] ppSpaRangeTbl pointer to a pointer where the table will be stored.

  @retval EFI_SUCCESS if the table was found and is properly returned.
  @retval EFI_INVALID_PARAMETER if pFitHead or ppInterleaveTbl equals NULL.
  @retval EFI_NOT_FOUND if there is no Interleave table with the provided index.
**/
EFI_STATUS
GetSpaRangeTable(
  IN     ParsedFitHeader *pFitHead,
  IN     UINT16 SpaRangeTblIndex,
     OUT SpaRangeTbl **ppSpaRangeTbl
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  UINT16 Index = 0;

  if (pFitHead == NULL || ppSpaRangeTbl == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  *ppSpaRangeTbl = NULL;

  for (Index = 0; Index < pFitHead->SpaRangeTblesNum; Index++) {
    if (pFitHead->ppSpaRangeTbles[Index]->SpaRangeDescriptionTableIndex == SpaRangeTblIndex) {
      *ppSpaRangeTbl = pFitHead->ppSpaRangeTbles[Index];
      ReturnCode = EFI_SUCCESS;
      break;
    }
  }

Finish:
  return ReturnCode;
}

/**
  GetInterleaveTable - returns the Interleave Table with the provided Index.

  @param[in] pFitHead pointer to the parsed NFit Header structure.
  @param[in] InterleaveTblIndex index of the table to be found.
  @param[out] ppInterleaveTbl pointer to a pointer where the table will be stored.

  @retval EFI_SUCCESS if the table was found and is properly returned.
  @retval EFI_INVALID_PARAMETER if pFitHead or ppInterleaveTbl equals NULL.
  @retval EFI_NOT_FOUND if there is no Interleave table with the provided index.
**/
EFI_STATUS
GetInterleaveTable(
  IN     ParsedFitHeader *pFitHead,
  IN     UINT16 InterleaveTblIndex,
     OUT InterleaveStruct **ppInterleaveTbl
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  UINT16 Index = 0;

  if (pFitHead == NULL || ppInterleaveTbl == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  *ppInterleaveTbl = NULL;

  for (Index = 0; Index < pFitHead->InterleaveTblesNum; Index++) {
    if (pFitHead->ppInterleaveTbles[Index]->InterleaveStructureIndex == InterleaveTblIndex) {
      *ppInterleaveTbl = pFitHead->ppInterleaveTbles[Index];
      ReturnCode = EFI_SUCCESS;
      break;
    }
  }

Finish:
  return ReturnCode;
}

/**
  Finds in the provided Nfit structure the requested NVDIMM region.

  If the pAddrRangeTypeGuid equals NULL, the first table matching the Pid will be returned.

  @param[in] pFitHead pointer to the parsed NFit Header structure.
  @param[in] Pid the Dimm ID that the NVDIMM region must be for.
  @param[in] pAddrRangeTypeGuid pointer to GUID type of the range that we are looking for. OPTIONAL
  @param[in] SpaRangeIndexProvided Determine if SpaRangeIndex is provided
  @param[in] SpaRangeIndex Looking for NVDIMM region table that is related with provided SPA table. OPTIONAL
  @param[out] ppNvDimmRegionMappingStructure pointer to a pointer for the return NVDIMM region.

  @retval EFI_SUCCESS if the table was found and was returned.
  @retval EFI_INVALID_PARAMETER if one or more input parameters equal NULL.
  @retval EFI_NOT_FOUND if there is no NVDIMM region for the provided Dimm PID and AddrRangeType.
**/
EFI_STATUS
GetNvDimmRegionMappingStructureForPid(
  IN     ParsedFitHeader *pFitHead,
  IN     UINT16 Pid,
  IN     GUID *pAddrRangeTypeGuid OPTIONAL,
  IN     BOOLEAN SpaRangeIndexProvided,
  IN     UINT16 SpaRangeIndex OPTIONAL,
     OUT NvDimmRegionMappingStructure **ppNvDimmRegionMappingStructure
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  UINT16 Index = 0;
  SpaRangeTbl *pSpaRangeTbl = NULL;
  UINT16 SpaIndexInNvDimmRegion = 0;
  BOOLEAN Found = FALSE;

  if (pFitHead == NULL || Pid == DIMM_PID_ALL || Pid == DIMM_PID_INVALID || ppNvDimmRegionMappingStructure == NULL) {
    goto Finish;
  }

  *ppNvDimmRegionMappingStructure = NULL;

  for (Index = 0; Index < pFitHead->NvDimmRegionMappingStructuresNum; Index++) {
    if (pFitHead->ppNvDimmRegionMappingStructures[Index]->NvDimmPhysicalId != Pid) {
      continue;
    }

    SpaIndexInNvDimmRegion = pFitHead->ppNvDimmRegionMappingStructures[Index]->SpaRangeDescriptionTableIndex;
    Found = TRUE;

    if (SpaRangeIndexProvided && SpaIndexInNvDimmRegion != SpaRangeIndex) {
      Found = FALSE;
    }

    if (pAddrRangeTypeGuid != NULL) {
      pSpaRangeTbl = NULL;
      ReturnCode = GetSpaRangeTable(pFitHead, SpaIndexInNvDimmRegion, &pSpaRangeTbl);

      if (EFI_ERROR(ReturnCode) || pSpaRangeTbl == NULL ||
          CompareMem(&pSpaRangeTbl->AddressRangeTypeGuid,
          pAddrRangeTypeGuid, sizeof(pSpaRangeTbl->AddressRangeTypeGuid)) != 0) {
        Found = FALSE;
      }
    }

    if (Found) {
      *ppNvDimmRegionMappingStructure = pFitHead->ppNvDimmRegionMappingStructures[Index];
      ReturnCode = EFI_SUCCESS;
      break;
    } else {
      ReturnCode = EFI_NOT_FOUND;
    }
  }

Finish:
  return ReturnCode;
}

/**
  RdpaToSpa() - Convert Device Region Physical to System Physical Address

  @param[in] Rdpa Device Region Physical Address to convert
  @param[in] pNvDimmRegionTable The NVDIMM region that helps describe this region of memory
  @param[in] pInterleaveTable Interleave table referenced by the mdsparng_tbl
  @param[out] SpaAddr output for SPA address

  A memory device could have multiple regions. As such we cannot convert
  to a device physical address. Instead we refer to the address for a region
  within the device as device region physical address (RDPA), where Rdpa is
  a zero based address from the start of the region within the device.

  @retval EFI_SUCCESS on success
  @retval EFI_INVALID_PARAMETER on a divide by zero error
**/
EFI_STATUS
RdpaToSpa(
  IN     UINT64 Rdpa,
  IN     NvDimmRegionMappingStructure *pNvDimmRegionTable,
  IN     SpaRangeTbl *pSpaRangeTable,
  IN     InterleaveStruct *pInterleaveTable OPTIONAL,
     OUT UINT64 *pSpaAddr
  )
{
  EFI_STATUS ReturnCode = EFI_SUCCESS;
  UINT64 RotationSize = 0;
  UINT64 RotationNum = 0;
  UINT32 LineNum = 0;
  UINT64 StartSpaAddress = 0;

  if (!pSpaAddr || !pSpaRangeTable) {
    return EFI_INVALID_PARAMETER;
  }

  StartSpaAddress = pSpaRangeTable->SystemPhysicalAddressRangeBase + pNvDimmRegionTable->RegionOffset;

  if (pInterleaveTable != NULL) {
    if (!pInterleaveTable->LineSize || !pInterleaveTable->NumberOfLinesDescribed) {
      NVDIMM_DBG("Divide by Zero\n");
      ReturnCode = EFI_INVALID_PARAMETER;
      return ReturnCode;
    }

    RotationSize = ((UINT64) pInterleaveTable->LineSize) * pInterleaveTable->NumberOfLinesDescribed;
    RotationNum = Rdpa / RotationSize;
    LineNum = (UINT32)((Rdpa % RotationSize) / pInterleaveTable->LineSize);

    *pSpaAddr = StartSpaAddress
        + RotationNum * RotationSize * pNvDimmRegionTable->InterleaveWays
        + pInterleaveTable->LinesOffsets[LineNum] * pInterleaveTable->LineSize
        + Rdpa % pInterleaveTable->LineSize;

    return ReturnCode;
  } else {
    /** TODO: Not Interleaved **/
    *pSpaAddr = StartSpaAddress + Rdpa;
    return ReturnCode;
  }
}

/**
  CopyMemoryAndAddPointerToArray - Copies the data and adds the result pointer to an array of pointers.

  @param[in, out] ppTable pointer to the array of pointers. Warning! This pointer will be freed.
  @param[in] pToAdd pointer to the data that the caller wants to add to the array.
  @param[in] DataSize size of the data that are supposed to be copied.
  @param[in] NewPointerIndex index in the table that the new pointer should have.

  @retval NULL - if a memory allocation failed.
  @retval pointer to the new array of pointers (with the new one at the end).
**/
STATIC
VOID **
CopyMemoryAndAddPointerToArray(
  IN OUT VOID **ppTable,
  IN     VOID *pToAdd,
  IN     UINT32 DataSize,
  IN     UINT32 *pNewPointerIndex
  )
{
  VOID **ppNewTable = NULL;
  VOID *pData = NULL;

  if (pToAdd == NULL) {
    NVDIMM_ERR("Pointer to data for adding cannot be NULL.");
    goto Finish;
  }

  // Allocate the memory for the new entry to list of tables and for the contents of new entry
  ppNewTable = AllocatePool(sizeof(VOID *) * (*pNewPointerIndex + 1));
  pData = AllocatePool(DataSize);

  if (ppNewTable == NULL || pData == NULL) {
    NVDIMM_DBG("Could not allocate the memory.");
    goto Finish;
  }

  // Copy the array beginning only if there is any
  if (*pNewPointerIndex > 0 && ppTable != NULL) {
    CopyMem_S(ppNewTable, sizeof(VOID *) * (*pNewPointerIndex + 1), ppTable, sizeof(VOID *) * *pNewPointerIndex);
  }

  // Make a copy of the table to add
  CopyMem_S(pData, DataSize, pToAdd, DataSize);

  // Assign the new copied table to the array
  ppNewTable[*pNewPointerIndex] = pData;

  (*pNewPointerIndex)++; // Increment the array index

Finish:
  FREE_POOL_SAFE(ppTable);

  return ppNewTable;
}


/**
  Return the current memory mode chosen by the BIOS during boot-up. 1LM is
  the fallback option and will always be available. 2LM will only be enabled
  if the AllowedMemoryMode is 2LM, there is memory configured for 2LM, and
  it is in a BIOS-supported configuration. We read this information from the
  PCAT table provided by BIOS.

  @param[out] pResult The current memory mode chosen by BIOS

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameter is NULL
  @retval EFI_ABORTED PCAT tables not found
**/
EFI_STATUS
CurrentMemoryMode(
     OUT MEMORY_MODE *pResult
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;

  NVDIMM_ENTRY();

  if (pResult == NULL) {
    goto Finish;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum != 1) {
    NVDIMM_DBG("Incorrect PCAT tables");
    ReturnCode = EFI_ABORTED;
    goto Finish;
  }

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPlatformConfigAttrTable)) {
    PLATFORM_CAPABILITY_INFO *pPlatformCapability = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo[0] != NULL) {
      pPlatformCapability = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no PlatformCapability table in PCAT.");
      ReturnCode = EFI_ABORTED;
      goto Finish;
    }
    *pResult = pPlatformCapability->CurrentMemoryMode.MemoryModeSplit.CurrentVolatileMode;
  }
  else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    PLATFORM_CAPABILITY_INFO3 *pPlatformCapability3 = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0] != NULL) {
      pPlatformCapability3 = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no PlatformCapability table in PCAT.");
      ReturnCode = EFI_ABORTED;
      goto Finish;
    }
    *pResult = pPlatformCapability3->CurrentMemoryMode.MemoryModeSplit.CurrentVolatileMode;
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    ReturnCode = EFI_ABORTED;
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Return the allowed memory mode selected in the BIOS setup menu under
  Socket Configuration -> Memory Configuration -> Memory Map -> Volatile Memory Mode.
  Even if 2LM is allowed, it implies that 1LM is allowed as well (even
  though the memory mode doesn't indicate this).
  We read this information from the PCAT table provided by BIOS.

  @param[out] pResult The allowed memory mode setting in BIOS

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameter is NULL
  @retval EFI_ABORTED PCAT tables not found
**/
EFI_STATUS
AllowedMemoryMode(
     OUT MEMORY_MODE *pResult
  )
{
  EFI_STATUS ReturnCode = EFI_INVALID_PARAMETER;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;

  NVDIMM_ENTRY();

  if (pResult == NULL) {
    goto Finish;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum != 1) {
    NVDIMM_DBG("Incorrect PCAT tables");
    ReturnCode = EFI_ABORTED;
    goto Finish;
  }

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPlatformConfigAttrTable)) {
    PLATFORM_CAPABILITY_INFO *pPlatformCapability = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo[0] != NULL) {
      pPlatformCapability = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no PlatformCapability table in PCAT.");
      ReturnCode = EFI_ABORTED;
      goto Finish;
    }
    *pResult = pPlatformCapability->CurrentMemoryMode.MemoryModeSplit.AllowedVolatileMode;
  }
  else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    PLATFORM_CAPABILITY_INFO3 *pPlatformCapability3 = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0] != NULL) {
      pPlatformCapability3 = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no PlatformCapability table in PCAT.");
      ReturnCode = EFI_ABORTED;
      goto Finish;
    }
    *pResult = pPlatformCapability3->CurrentMemoryMode.MemoryModeSplit.AllowedVolatileMode;
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    ReturnCode = EFI_ABORTED;
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Check if BIOS supports changing configuration through management software

  @param[out] pConfigChangeSupported The Config Change support in BIOS

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameter is NULL
  @retval EFI_LOAD_ERROR PCAT tables not found
**/
EFI_STATUS
CheckIfBiosSupportsConfigChange(
     OUT BOOLEAN *pConfigChangeSupported
  )
{
  EFI_STATUS ReturnCode = EFI_LOAD_ERROR;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;

  NVDIMM_ENTRY();

  if (pConfigChangeSupported == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  *pConfigChangeSupported = FALSE;

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum != 1) {
    NVDIMM_DBG("Incorrect PCAT tables");
    goto Finish;
  }

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPlatformConfigAttrTable)) {
    PLATFORM_CAPABILITY_INFO *pPlatformCapability = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo[0] != NULL) {
      pPlatformCapability = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no PlatformCapability table in PCAT.");
      goto Finish;
    }

    if (IS_BIT_SET_VAR(pPlatformCapability->MgmtSwConfigInputSupport, BIT0)) {
      *pConfigChangeSupported = TRUE;
    }
  }
  else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    PLATFORM_CAPABILITY_INFO3 *pPlatformCapability3 = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0] != NULL) {
      pPlatformCapability3 = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no PlatformCapability table in PCAT.");
      goto Finish;
    }

    if (IS_BIT_SET_VAR(pPlatformCapability3->MgmtSwConfigInputSupport, BIT0)) {
      *pConfigChangeSupported = TRUE;
    }
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Check Memory Mode Capabilties from PCAT table type 0

  @param[out] pMemoryModeCapabilities pointer to memory mode capabilites

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameter is NULL
  @retval EFI_LOAD_ERROR PCAT tables not found
**/
EFI_STATUS
CheckMemModeCapabilities(
  OUT MEMORY_MODE_CAPABILITIES *pMemoryModeCapabilities
  )
{
  EFI_STATUS ReturnCode = EFI_LOAD_ERROR;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;

  NVDIMM_ENTRY();

  if (pMemoryModeCapabilities == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum != 1) {
    NVDIMM_DBG("Incorrect PCAT tables");
    goto Finish;
  }

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPlatformConfigAttrTable)) {
    PLATFORM_CAPABILITY_INFO *pPlatformCapability = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo[0] != NULL) {
      pPlatformCapability = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppPlatformCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no PlatformCapability table in PCAT.");
      goto Finish;
    }

    CopyMem_S(pMemoryModeCapabilities, sizeof(MEMORY_MODE_CAPABILITIES),
      &pPlatformCapability->MemoryModeCapabilities, sizeof(MEMORY_MODE_CAPABILITIES));
    // Backwards compatability. Platforms with PCAT revison < 1.x always support mixed mode
    pMemoryModeCapabilities->MemoryModesFlags.MixedMode = MIXED_MODE_CAPABILITY_SUPPORTED;
  }
  else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    PLATFORM_CAPABILITY_INFO3 *pPlatformCapability3 = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0] != NULL) {
      pPlatformCapability3 = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no PlatformCapability table in PCAT.");
      goto Finish;
    }

    CopyMem_S(pMemoryModeCapabilities, sizeof(MEMORY_MODE_CAPABILITIES),
      &pPlatformCapability3->MemoryModeCapabilities, sizeof(MEMORY_MODE_CAPABILITIES));
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Retrieve the PCAT Socket SKU Mapped Memory Limit for a given socket

  @param[in] SocketId SocketID
  @param[out] pMappedMemoryLimit Pointer to Mapped Memory Limit

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameter is NULL
  @retval EFI_NOT_FOUND PCAT socket sku mapped memory limit not found for given socketID
**/
EFI_STATUS
RetrievePcatSocketSkuMappedMemoryLimit(
  IN     UINT32 SocketId,
  OUT    UINT64 *pMappedMemoryLimit
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;
  UINT32 Index = 0;
  UINT32 LogicalSocketID = 0;

  NVDIMM_ENTRY();

  if (pMappedMemoryLimit == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->SocketSkuInfoNum == 0) {
    NVDIMM_DBG("Incorrect PCAT tables");
    goto Finish;
  }

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPlatformConfigAttrTable)) {
    for (Index = 0; Index < gNvmDimmData->PMEMDev.pPcatHead->SocketSkuInfoNum; Index++) {
      if (SocketId == gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppSocketSkuInfoTable[Index]->SocketId) {
        *pMappedMemoryLimit = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppSocketSkuInfoTable[Index]->MappedMemorySizeLimit;
        ReturnCode = EFI_SUCCESS;
        break;
      }
    }
  }
  else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    for (Index = 0; Index < gNvmDimmData->PMEMDev.pPcatHead->SocketSkuInfoNum; Index++) {
      ReturnCode = GetLogicalSocketIdFromPmtt(gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable[Index]->SocketId,
        gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable[Index]->DieId, &LogicalSocketID);
      if (EFI_ERROR(ReturnCode)) {
        NVDIMM_DBG("Uanble to retrieve logical socket ID");
        goto Finish;
      }
      if (SocketId == LogicalSocketID) {
        *pMappedMemoryLimit = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable[Index]->MappedMemorySizeLimit;
        ReturnCode = EFI_SUCCESS;
        break;
      }
    }
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    goto Finish;
  }

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Retrieve the PCAT Socket SKU Total Mapped Memory for a given socket

  @param[in] SocketId SocketID, 0xFFFF indicates all sockets
  @param[out] pTotalMappedMemory Pointer to Total Mapped Memory

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameter is NULL
  @retval EFI_NOT_FOUND PCAT socket sku total mapped memory not found for given socketID
**/
EFI_STATUS
RetrievePcatSocketSkuTotalMappedMemory(
  IN     UINT32 SocketId,
  OUT    UINT64 *pTotalMappedMemory
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;
  UINT32 Index = 0;
  UINT32 LogicalSocketID = 0;

  NVDIMM_ENTRY();

  if (pTotalMappedMemory == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->SocketSkuInfoNum == 0) {
    NVDIMM_DBG("Incorrect PCAT tables");
    goto Finish;
  }

  // Setting output parameters to 0 before initialization
  *pTotalMappedMemory = 0;

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPlatformConfigAttrTable)) {
    for (Index = 0; Index < gNvmDimmData->PMEMDev.pPcatHead->SocketSkuInfoNum; Index++) {
      if (SocketId == SOCKET_ID_ALL || SocketId == gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppSocketSkuInfoTable[Index]->SocketId) {
        *pTotalMappedMemory += gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppSocketSkuInfoTable[Index]->TotalMemorySizeMappedToSpa;
        ReturnCode = EFI_SUCCESS;
        if (SocketId != SOCKET_ID_ALL) {
          break;
        }
      }
    }
  }
  else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    for (Index = 0; Index < gNvmDimmData->PMEMDev.pPcatHead->SocketSkuInfoNum; Index++) {
      ReturnCode = GetLogicalSocketIdFromPmtt(gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable[Index]->SocketId,
        gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable[Index]->DieId, &LogicalSocketID);
      if (EFI_ERROR(ReturnCode)) {
        NVDIMM_DBG("Uanble to retrieve logical socket ID");
        goto Finish;
      }
      if (SocketId == SOCKET_ID_ALL || SocketId == LogicalSocketID) {
        *pTotalMappedMemory += gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable[Index]->TotalMemorySizeMappedToSpa;
        ReturnCode = EFI_SUCCESS;
        if (SocketId != SOCKET_ID_ALL) {
          break;
        }
      }
    }
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    goto Finish;
  }

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Retrieve the PCAT Socket SKU Cached Memory for a given socket

  @param[in] SocketId SocketID, 0xFFFF indicates all sockets
  @param[out] pCachedMemory Pointer to Cached Memory Size

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER Input parameter is NULL
  @retval EFI_NOT_FOUND PCAT socket sku cached memory size not found for given socketID
**/
EFI_STATUS
RetrievePcatSocketSkuCachedMemory(
  IN     UINT32 SocketId,
  OUT    UINT64 *pCachedMemory
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;
  UINT32 Index = 0;
  UINT32 LogicalSocketID = 0;

  NVDIMM_ENTRY();

  if (pCachedMemory == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->SocketSkuInfoNum == 0) {
    NVDIMM_DBG("Incorrect PCAT tables");
    goto Finish;
  }

  // Setting output parameters to 0 before initialization
  *pCachedMemory = 0;

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPlatformConfigAttrTable)) {
    for (Index = 0; Index < gNvmDimmData->PMEMDev.pPcatHead->SocketSkuInfoNum; Index++) {
      if (SocketId == SOCKET_ID_ALL || SocketId == gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppSocketSkuInfoTable[Index]->SocketId) {
        *pCachedMemory += gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppSocketSkuInfoTable[Index]->CachingMemorySize;
        ReturnCode = EFI_SUCCESS;
        if (SocketId != SOCKET_ID_ALL) {
          break;
        }
      }
    }
  }
  else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    for (Index = 0; Index < gNvmDimmData->PMEMDev.pPcatHead->SocketSkuInfoNum; Index++) {
      ReturnCode = GetLogicalSocketIdFromPmtt(gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable[Index]->SocketId,
        gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable[Index]->DieId, &LogicalSocketID);
      if (EFI_ERROR(ReturnCode)) {
        NVDIMM_DBG("Uanble to retrieve logical socket ID");
        goto Finish;
      }
      if (SocketId == SOCKET_ID_ALL || SocketId == LogicalSocketID) {
        *pCachedMemory += gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppDieSkuInfoTable[Index]->CachingMemorySize;
        ReturnCode = EFI_SUCCESS;
        if (SocketId != SOCKET_ID_ALL) {
          break;
        }
      }
    }
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    goto Finish;
  }

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Retrieve the list of supported Channel & iMC Interleave sizes

  @param[out] ppChannelInterleaveSize Array of supported Channel Interleave sizes
  @param[out] ppiMCInterleaveSize Array of supported iMC Interleave sizes
  @param[out] ppRecommendedFormats Array of recommended formats
  @param[out] ppChannelWays Array of supported channel ways
  @param[out] pLength Length of the array
  @param[out] pInterleaveAlignmentSize Interleave Alignment Size
  @param[out] pRevision PCAT Table revision

  @retval EFI_SUCCESS Success
  @retval EFI_OUT_OF_RESOURCES Memory Allocation failure
  @retval EFI_INVALID_PARAMETER ppChannelInterleaveSize, ppiMCInterleaveSize or pLength is NULL
  @retval EFI_NOT_FOUND Interleave size info not found
**/
EFI_STATUS
RetrieveSupportediMcAndChannelInterleaveSizes(
  OUT  UINT32 **ppChannelInterleaveSize,
  OUT  UINT32 **ppiMCInterleaveSize,
  OUT  UINT32 **ppRecommendedFormats,
  OUT  UINT32 **ppChannelWays,
  OUT  UINT32 *pLength,
  OUT  UINT32 *pInterleaveAlignmentSize,
  OUT  ACPI_REVISION *pRevision
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;
  BOOLEAN RetrieveRecommendedFormats = FALSE;
  BOOLEAN RetrieveChannelWays = FALSE;
  BOOLEAN RetrieveInterleaveAlignmentSize = FALSE;
  UINT32 Index = 0;

  NVDIMM_ENTRY();

  if (ppChannelInterleaveSize == NULL || ppiMCInterleaveSize == NULL || pLength == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  if (ppRecommendedFormats != NULL) {
    RetrieveRecommendedFormats = TRUE;
  }

  if (ppChannelWays != NULL) {
    RetrieveChannelWays = TRUE;
  }

  if (pInterleaveAlignmentSize != NULL) {
    RetrieveInterleaveAlignmentSize = TRUE;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum != 1) {
    NVDIMM_DBG("Incorrect PCAT tables");
    goto Finish;
  }

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (pRevision != NULL) {
    *pRevision = pPlatformConfigAttrTable->Header.Revision;
  }

  if (IS_ACPI_HEADER_REV_MAJ_0_MIN_VALID(pPlatformConfigAttrTable)) {
    MEMORY_INTERLEAVE_CAPABILITY_INFO *pMemoryInterleaveCapability = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->MemoryInterleaveCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppMemoryInterleaveCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppMemoryInterleaveCapabilityInfo[0] != NULL) {
      for (Index = 0; Index < gNvmDimmData->PMEMDev.pPcatHead->MemoryInterleaveCapabilityInfoNum; Index++) {
        if (gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppMemoryInterleaveCapabilityInfo[Index]->MemoryMode
          == PCAT_MEMORY_MODE_PM_DIRECT) {
          pMemoryInterleaveCapability = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppMemoryInterleaveCapabilityInfo[Index];
          break;
        }
      }

      if (pMemoryInterleaveCapability == NULL) {
        NVDIMM_DBG("There is no Memory Interleave Capability Information table for PM mode.");
        goto Finish;
      }
    }
    else {
      NVDIMM_DBG("There is no MemoryInterleaveCapability table in PCAT.");
      goto Finish;
    }

    *ppChannelInterleaveSize = AllocateZeroPool(sizeof(**ppChannelInterleaveSize) * pMemoryInterleaveCapability->NumOfFormatsSupported);
    if (*ppChannelInterleaveSize == NULL) {
      ReturnCode = EFI_OUT_OF_RESOURCES;
      goto Finish;
    }

    *ppiMCInterleaveSize = AllocateZeroPool(sizeof(**ppiMCInterleaveSize) * pMemoryInterleaveCapability->NumOfFormatsSupported);
    if (*ppiMCInterleaveSize == NULL) {
      ReturnCode = EFI_OUT_OF_RESOURCES;
      goto Finish;
    }

    if (RetrieveRecommendedFormats) {
      *ppRecommendedFormats = AllocateZeroPool(sizeof(**ppRecommendedFormats) * pMemoryInterleaveCapability->NumOfFormatsSupported);
      if (*ppRecommendedFormats == NULL) {
        ReturnCode = EFI_OUT_OF_RESOURCES;
        goto Finish;
      }
    }

    if (RetrieveChannelWays) {
      *ppChannelWays = AllocateZeroPool(sizeof(**ppChannelWays) * pMemoryInterleaveCapability->NumOfFormatsSupported);
      if (*ppChannelWays == NULL) {
        ReturnCode = EFI_OUT_OF_RESOURCES;
        goto Finish;
      }
    }

    for (Index = 0; Index < pMemoryInterleaveCapability->NumOfFormatsSupported; Index++) {
      (*ppiMCInterleaveSize)[Index] = pMemoryInterleaveCapability->InterleaveFormatList[Index].InterleaveFormatSplit.iMCInterleaveSize;
      (*ppChannelInterleaveSize)[Index] = pMemoryInterleaveCapability->InterleaveFormatList[Index].InterleaveFormatSplit.ChannelInterleaveSize;
      if (RetrieveChannelWays) {
        (*ppChannelWays)[Index] = pMemoryInterleaveCapability->InterleaveFormatList[Index].InterleaveFormatSplit.NumberOfChannelWays;
      }
      if (RetrieveRecommendedFormats) {
        (*ppRecommendedFormats)[Index] = pMemoryInterleaveCapability->InterleaveFormatList[Index].InterleaveFormatSplit.Recommended;
      }
    }
    *pLength = Index;

    if (RetrieveInterleaveAlignmentSize) {
      *pInterleaveAlignmentSize = pMemoryInterleaveCapability->InterleaveAlignmentSize;
    }
  }
  else if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    MEMORY_INTERLEAVE_CAPABILITY_INFO3 *pMemoryInterleaveCapability3 = NULL;
    if (gNvmDimmData->PMEMDev.pPcatHead->MemoryInterleaveCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo[0] != NULL) {
      for (Index = 0; Index < gNvmDimmData->PMEMDev.pPcatHead->MemoryInterleaveCapabilityInfoNum; Index++) {
        if (gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat2Tables.ppMemoryInterleaveCapabilityInfo[Index]->MemoryMode
          == PCAT_MEMORY_MODE_PM_DIRECT) {
          pMemoryInterleaveCapability3 = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo[Index];
          break;
        }
      }

      if (pMemoryInterleaveCapability3 == NULL) {
        NVDIMM_DBG("There is no Memory Interleave Capability Information table for PM mode.");
        goto Finish;
      }
    }
    else {
      NVDIMM_DBG("There is no MemoryInterleaveCapability table in PCAT.");
      goto Finish;
    }

    *ppChannelInterleaveSize = AllocateZeroPool(sizeof(**ppChannelInterleaveSize));
    if (*ppChannelInterleaveSize == NULL) {
      ReturnCode = EFI_OUT_OF_RESOURCES;
      goto Finish;
    }

    *ppiMCInterleaveSize = AllocateZeroPool(sizeof(**ppiMCInterleaveSize));
    if (*ppiMCInterleaveSize == NULL) {
      ReturnCode = EFI_OUT_OF_RESOURCES;
      goto Finish;
    }

    (*ppiMCInterleaveSize)[Index] = pMemoryInterleaveCapability3->InterleaveSize.InterleaveSizeSplit.iMCInterleaveSize;
    (*ppChannelInterleaveSize)[Index] = pMemoryInterleaveCapability3->InterleaveSize.InterleaveSizeSplit.ChannelInterleaveSize;
    if (RetrieveInterleaveAlignmentSize) {
      *pInterleaveAlignmentSize = pMemoryInterleaveCapability3->InterleaveAlignmentSize;
    }

    if (RetrieveChannelWays) {
      *ppChannelWays = NULL;
    }

    if (RetrieveRecommendedFormats) {
      *ppRecommendedFormats = NULL;
    }

    *pLength = 1;
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    ReturnCode = EFI_ABORTED;
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Retrieve InterleaveSetMap Info

  @param[out] ppInterleaveMap Info List used to determine the best interleave based on requested DCPMMs
  @param[out] pInterleaveMapListLength Pointer to the InterleaveSetMap Length

  @retval EFI_SUCCESS Success
  @retval EFI_OUT_OF_RESOURCES Memory Allocation failure
  @retval EFI_INVALID_PARAMETER ppInterleaveSetMap or InterleaveMapListLength is NULL
  @retval EFI_NOT_FOUND InterleaveSetMap Info not found
**/
EFI_STATUS
RetrieveInterleaveSetMap(
  OUT  UINT32 **ppInterleaveMap,
  OUT  UINT32 *pInterleaveMapListLength
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;
  MEMORY_INTERLEAVE_CAPABILITY_INFO3 *pMemoryInterleaveCapability = NULL;
  UINT32 Index = 0;

  NVDIMM_ENTRY();

  if (ppInterleaveMap == NULL || pInterleaveMapListLength == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->MemoryInterleaveCapabilityInfoNum != 1) {
    NVDIMM_DBG("Incorrect PCAT tables");
    goto Finish;
  }

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    if (gNvmDimmData->PMEMDev.pPcatHead->MemoryInterleaveCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo[0] != NULL) {
      pMemoryInterleaveCapability = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no MemoryInterleaveCapability table in PCAT.");
      goto Finish;
    }

    *ppInterleaveMap = AllocateZeroPool(sizeof(**ppInterleaveMap) * pMemoryInterleaveCapability->NumOfFormatsSupported);
    if (*ppInterleaveMap == NULL) {
      ReturnCode = EFI_OUT_OF_RESOURCES;
      goto Finish;
    }

    *pInterleaveMapListLength = 0;
    for (Index = 0; Index < pMemoryInterleaveCapability->NumOfFormatsSupported; Index++) {
      (*ppInterleaveMap)[Index] = pMemoryInterleaveCapability->InterleaveFormatList[Index].InterleaveFormatSplit.InterleaveMap;
    }
    *pInterleaveMapListLength = Index;
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Retrieve Channel ways from InterleaveSetMap Info

  @param[out] ppChannelWays Array of channel ways supported
  @param[out] pChannelWaysListLength Pointer to the ppChannelWays array Length

  @retval EFI_SUCCESS Success
  @retval EFI_OUT_OF_RESOURCES Memory Allocation failure
  @retval EFI_INVALID_PARAMETER ppInterleaveSetMap or InterleaveMapListLength is NULL
  @retval EFI_NOT_FOUND InterleaveSetMap Info not found
**/
EFI_STATUS
RetrieveChannelWaysFromInterleaveSetMap(
  OUT  UINT32 **ppChannelWays,
  OUT  UINT32 *pChannelWaysListLength
)
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;
  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;
  MEMORY_INTERLEAVE_CAPABILITY_INFO3 *pMemoryInterleaveCapability = NULL;
  UINT32 Index = 0;
  UINT8 NumOfBitsSet = 0;
  UINT8 PrevNumOfBitsSet = 0;
  UINT32 Length = 0;

  NVDIMM_ENTRY();

  if (ppChannelWays == NULL || pChannelWaysListLength == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->MemoryInterleaveCapabilityInfoNum != 1) {
    NVDIMM_DBG("Incorrect PCAT tables");
    goto Finish;
  }

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    if (gNvmDimmData->PMEMDev.pPcatHead->MemoryInterleaveCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo[0] != NULL) {
      pMemoryInterleaveCapability = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppMemoryInterleaveCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no MemoryInterleaveCapability table in PCAT.");
      goto Finish;
    }

    *pChannelWaysListLength = 0;
    for (Index = 0; Index < pMemoryInterleaveCapability->NumOfFormatsSupported; Index++) {
      ReturnCode  = CountNumOfBitsSet(pMemoryInterleaveCapability->InterleaveFormatList[Index].InterleaveFormatSplit.InterleaveMap, &NumOfBitsSet);
      if (EFI_ERROR(ReturnCode)) {
        NVDIMM_DBG("CountNumOfBitsSet failed");
        goto Finish;
      }

      if (PrevNumOfBitsSet == NumOfBitsSet) {
        continue;
      }

      *ppChannelWays = ReallocatePool(sizeof(UINT32) * Length, sizeof(UINT32) * (Length + 1), *ppChannelWays);
      if (*ppChannelWays == NULL) {
        ReturnCode = EFI_OUT_OF_RESOURCES;
        goto Finish;
      }

      switch (NumOfBitsSet) {
      case 1:
        (*ppChannelWays)[Length] = INTERLEAVE_SET_1_WAY;
        break;
      case 2:
        (*ppChannelWays)[Length] = INTERLEAVE_SET_2_WAY;
        break;
      case 3:
        (*ppChannelWays)[Length] = INTERLEAVE_SET_3_WAY;
        break;
      case 4:
        (*ppChannelWays)[Length] = INTERLEAVE_SET_4_WAY;
        break;
      case 6:
        (*ppChannelWays)[Length] = INTERLEAVE_SET_6_WAY;
        break;
      case 8:
        (*ppChannelWays)[Length] = INTERLEAVE_SET_8_WAY;
        break;
      case 12:
        (*ppChannelWays)[Length] = INTERLEAVE_SET_12_WAY;
        break;
      case 16:
        (*ppChannelWays)[Length] = INTERLEAVE_SET_16_WAY;
        break;
      case 24:
        (*ppChannelWays)[Length] = INTERLEAVE_SET_24_WAY;
        break;
      default:
        NVDIMM_WARN("Unsupported number of DIMMs in interleave set: %d", NumOfBitsSet);
        (*ppChannelWays)[Length] = 0;
        break;
      }
      PrevNumOfBitsSet = NumOfBitsSet;
      Length++;
    }
    // BIOS does not include x1 (non-interleaved)
    // since it is always supported
    *ppChannelWays = ReallocatePool(sizeof(UINT32) * Length, sizeof(UINT32) * (Length + 1), *ppChannelWays);
    if (*ppChannelWays == NULL) {
      ReturnCode = EFI_OUT_OF_RESOURCES;
      goto Finish;
    }
    (*ppChannelWays)[Length] = INTERLEAVE_SET_1_WAY;
    Length++;
    *pChannelWaysListLength = Length;
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

/**
  Retrieve Maximum PM Interleave Sets per Die & DCPMM

  @param[out] pMaxPMInterleaveSets pointer to Maximum PM Interleave Sets per Die & Dcpmm

  @retval EFI_SUCCESS Success
  @retval EFI_INVALID_PARAMETER pMaxPMInterleaveSetsPerDie is NULL
  @retval EFI_NOT_FOUND InterleaveSetMap Info not found
**/
EFI_STATUS
RetrieveMaxPMInterleaveSets(
  OUT  MAX_PMINTERLEAVE_SETS *pMaxPMInterleaveSets
  )
{
  EFI_STATUS ReturnCode = EFI_NOT_FOUND;

  PLATFORM_CONFIG_ATTRIBUTES_TABLE *pPlatformConfigAttrTable = NULL;
  PLATFORM_CAPABILITY_INFO3 *pPlatformCapabilityInfo = NULL;

  NVDIMM_ENTRY();

  if (pMaxPMInterleaveSets == NULL) {
    ReturnCode = EFI_INVALID_PARAMETER;
    goto Finish;
  }

  if (gNvmDimmData->PMEMDev.pPcatHead == NULL || gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum != 1) {
    NVDIMM_DBG("Incorrect PCAT tables");
    goto Finish;
  }

  pPlatformConfigAttrTable = gNvmDimmData->PMEMDev.pPcatHead->pPlatformConfigAttr;
  if (IS_ACPI_HEADER_REV_MAJ_1_MIN_VALID(pPlatformConfigAttrTable)) {
    if (gNvmDimmData->PMEMDev.pPcatHead->PlatformCapabilityInfoNum == 1 &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo != NULL &&
      gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0] != NULL) {
      pPlatformCapabilityInfo = gNvmDimmData->PMEMDev.pPcatHead->pPcatVersion.Pcat3Tables.ppPlatformCapabilityInfo[0];
    }
    else {
      NVDIMM_DBG("There is no PlatformCapabilityInfo table in PCAT.");
      goto Finish;
    }

    CopyMem(pMaxPMInterleaveSets, &pPlatformCapabilityInfo->MaxPMInterleaveSets, sizeof(*pMaxPMInterleaveSets));
  }
  else {
    NVDIMM_DBG("Unknown PCAT table revision");
    goto Finish;
  }

  ReturnCode = EFI_SUCCESS;

Finish:
  NVDIMM_EXIT_I64(ReturnCode);
  return ReturnCode;
}

