/*
 * TransportAnalyzer: a simple WFP KMD for analyzing incoming and outgoing TCP packets.
 * Copyright (C) 2021 dxboats
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */



#include "callouts.h"



UINT32 gCalloutOutboundIdentifier;



/*
 * Creates the outbound callout with the provided filter engine handle and device object pointer.
 */
NTSTATUS taCreateCalloutOutbound(_In_ HANDLE engineHandle, _In_ PDEVICE_OBJECT pDeviceObject) {
	NTSTATUS status = STATUS_SUCCESS;

	FWPS_CALLOUT calloutRegister = {
		.calloutKey = TA_CALLOUT_OUTBOUND_GUID,
		.flags = 0,
		.classifyFn = taClassifyOutbound,
		.notifyFn = taNotifyOutbound,
		.flowDeleteFn = taFlowDeleteOutbound,
	};

	status = FwpsCalloutRegister(pDeviceObject, &calloutRegister, &gCalloutOutboundIdentifier);
	TA_DEBUG_ONLY_LOG_ERROR_STATUS("failed to register outbound callout", status);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	FWPM_CALLOUT calloutAdd = {
		.calloutKey = TA_CALLOUT_OUTBOUND_GUID,
		.displayData = {
			.name = TA_CALLOUT_OUTBOUND_NAME,
			.description = TA_CALLOUT_OUTBOUND_DESCRIPTION,
		},
		.flags = 0,
		.applicableLayer = FWPM_LAYER_OUTBOUND_TRANSPORT_V4,
		.calloutId = gCalloutOutboundIdentifier,
	};

	status = FwpmCalloutAdd(engineHandle, &calloutAdd, NULL, NULL);
	TA_DEBUG_ONLY_LOG_ERROR_STATUS("failed to add outbound callout to filter engine", status);
	return status;
}



/*
 * Deletes the outbound callout with the provided filter engine handle and device object pointer.
 * Unsure if the callout needs to be both unregistered and deleted?
 */
NTSTATUS taDeleteCalloutOutbound(_In_ HANDLE engineHandle, _In_ PDEVICE_OBJECT pDeviceObject) {
	UNREFERENCED_PARAMETER(pDeviceObject);
	NTSTATUS status = STATUS_SUCCESS;
	status = FwpsCalloutUnregisterById(gCalloutOutboundIdentifier);
	TA_DEBUG_ONLY_LOG_ERROR_STATUS("failed to unregister outbound callout", status);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	status = FwpmCalloutDeleteByKey(engineHandle, &TA_CALLOUT_OUTBOUND_GUID);
	TA_DEBUG_ONLY_LOG_ERROR_STATUS("failed to delete outbound callout from filter engine", status);
	return status;
}



/*
 * Always allows the packet through. Will print the packet's payload and TCP header flags.
 */
VOID NTAPI taClassifyOutbound(
	_In_ const FWPS_INCOMING_VALUES* pInFixedValues,
	_In_ const FWPS_INCOMING_METADATA_VALUES* pInMetaValues,
	_Inout_opt_ void* pLayerData,
	_In_opt_ const void* pClassifyContext,
	_In_ const FWPS_FILTER* pFilter, _In_ UINT64 flowContext, _Inout_ FWPS_CLASSIFY_OUT* pClassifyOut
) {
	// Unreferenced parameters
	UNREFERENCED_PARAMETER(pClassifyContext);
	UNREFERENCED_PARAMETER(pFilter);
	UNREFERENCED_PARAMETER(flowContext);

	// Track progression of callout processing
	BOOL bufferAllocated = FALSE;
	ULONG transportHeaderSize = 0;
	PNET_BUFFER_LIST pNetBufferList = NULL;
	PNET_BUFFER pNetBuffer = NULL;
	PBYTE pAllocatedBuffer = NULL;

	// Ensure that the transport header size is specified within the meta values
	if (!FWPS_IS_METADATA_FIELD_PRESENT(pInMetaValues, FWPS_METADATA_FIELD_TRANSPORT_HEADER_SIZE)) {
		goto finalize;
	}

	// Ensure that there is layer data
	if (!pLayerData) {
		goto finalize;
	}

	// Get the local port and address
	UINT16 localPort = pInFixedValues->incomingValue[FWPS_FIELD_OUTBOUND_TRANSPORT_V4_IP_LOCAL_PORT].value.uint16;
	UINT32 localAddress = pInFixedValues->incomingValue[FWPS_FIELD_OUTBOUND_TRANSPORT_V4_IP_LOCAL_ADDRESS].value.uint32;

	// Get the remote port and address
	UINT16 remotePort = pInFixedValues->incomingValue[FWPS_FIELD_OUTBOUND_TRANSPORT_V4_IP_REMOTE_PORT].value.uint16;
	UINT32 remoteAddress = pInFixedValues->incomingValue[FWPS_FIELD_OUTBOUND_TRANSPORT_V4_IP_REMOTE_ADDRESS].value.uint32;

	// Retrieve the transport header size meta value
	transportHeaderSize = pInMetaValues->transportHeaderSize;

	// Get the first net buffer
	pNetBufferList = (PNET_BUFFER_LIST) pLayerData;
	pNetBuffer = NET_BUFFER_LIST_FIRST_NB(pNetBufferList);

	// Get the length of the packet's data AND TCP header
	// Notice, that the length includes the header this time
	ULONG packetLength = NET_BUFFER_DATA_LENGTH(pNetBuffer);

	// Attempt to allocate a buffer for the TCP header and data
	// In the real world, you probably would want to allocate up to the TCP header, read its contents, then
	// make a call to NdisAdvanceNetBufferDataStart to start right at the data section of the packet.
	// Don't forget to retreat the buffer back when you're done if you decide to go this route!
	pAllocatedBuffer = (PBYTE) ExAllocatePoolZero(NonPagedPoolNx, (size_t) packetLength, TA_CALLOUT_INBOUND_POOL_TAG);
	if (!pAllocatedBuffer) {
		goto finalize;
	}
	bufferAllocated = TRUE;

	// Attempt to get a contiguous buffer from which the TCP header and data is actually read from
	PBYTE pContiguousBuffer = (PBYTE) NdisGetDataBuffer(pNetBuffer, (ULONG) packetLength, pAllocatedBuffer, 1, 0);
	if (!pContiguousBuffer) {
		goto finalize;
	}

	// Get the TCP packet's checksum (I guess it's always zero when at this layer)
	UINT16 checksum = taTCPGetChecksum(pContiguousBuffer);

	// Indicate the beginning of a packet
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "BEGIN PACKET: %04x\n", checksum);

	// Indicate direction of packet
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
		"Direction: %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d",
		PRETTY_ADDRESS(localAddress), localPort,
		PRETTY_ADDRESS(remoteAddress), remotePort
	);

	// Print the TCP Flags
	taTCPPrintFlags(pContiguousBuffer);

	// Print data if there is some
	if (packetLength != transportHeaderSize) {
		DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "Data:\n");

		// Print each byte of data (in little-endian)
		for (ULONG index = 0; index < packetLength; index++) {
			BYTE value = *(transportHeaderSize + pContiguousBuffer + index);
			CHAR byteString[9];
			taTCPPrettyByte(value, byteString);
			DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "(%04d) (0x%02x) %s\n", value, value, byteString);
		}
	}

	// Indicate the ending of the packet
	DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "END PACKET: %04x\n", checksum);

finalize:
	// Ensure that the buffer is freed
	if (bufferAllocated) {
		ExFreePoolWithTag((PVOID) pAllocatedBuffer, TA_CALLOUT_INBOUND_POOL_TAG);
		pAllocatedBuffer = 0;
	}

	// Default action is to permit
	pClassifyOut->actionType = FWP_ACTION_PERMIT;
}



/*
 * Doesn't do anything. There currently is no need to listen to callout events.
 */
NTSTATUS NTAPI taNotifyOutbound(_In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType, _In_ const GUID* pFilterKey, _Inout_ FWPS_FILTER* pFilter) {
	UNREFERENCED_PARAMETER(notifyType);
	UNREFERENCED_PARAMETER(pFilterKey);
	UNREFERENCED_PARAMETER(pFilter);
	return STATUS_SUCCESS;
}



/*
 * Doesn't do anything. There currently aren't any resources that need to be cleaned up once the data is stopped.
 */
VOID NTAPI taFlowDeleteOutbound(_In_ UINT16 layerId, _In_ UINT32 calloutId, _In_ UINT64 flowContext) {
	UNREFERENCED_PARAMETER(layerId);
	UNREFERENCED_PARAMETER(calloutId);
	UNREFERENCED_PARAMETER(flowContext);
}