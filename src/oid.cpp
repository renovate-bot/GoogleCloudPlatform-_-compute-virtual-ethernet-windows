// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "oid.h"  // NOLINT: include directory

#include <ndis.h>

#include "adapter.h"   // NOLINT: include directory
#include "miniport.h"  // NOLINT: include directory
#include "netutils.h"  // NOLINT: include directory
#include "trace.h"     // NOLINT: include directory

#include "oid.tmh"  // NOLINT: trace message header

namespace {
const char* GetOidName(NDIS_OID oid) {
// Use stringifying to convert OID to string constant.
#define MAKE_CASE_TO_STRING(id) \
  case id:                      \
    return #id;
  switch (oid) {
    MAKE_CASE_TO_STRING(OID_GEN_STATISTICS)
    MAKE_CASE_TO_STRING(OID_GEN_XMIT_OK)
    MAKE_CASE_TO_STRING(OID_GEN_RCV_OK)
    MAKE_CASE_TO_STRING(OID_GEN_TRANSMIT_BUFFER_SPACE)
    MAKE_CASE_TO_STRING(OID_GEN_RECEIVE_BUFFER_SPACE)
    MAKE_CASE_TO_STRING(OID_GEN_TRANSMIT_BLOCK_SIZE)
    MAKE_CASE_TO_STRING(OID_GEN_RECEIVE_BLOCK_SIZE)
    MAKE_CASE_TO_STRING(OID_GEN_VENDOR_ID)
    MAKE_CASE_TO_STRING(OID_GEN_VENDOR_DESCRIPTION)
    MAKE_CASE_TO_STRING(OID_GEN_VENDOR_DRIVER_VERSION)
    MAKE_CASE_TO_STRING(OID_GEN_CURRENT_PACKET_FILTER)
    MAKE_CASE_TO_STRING(OID_GEN_CURRENT_LOOKAHEAD)
    MAKE_CASE_TO_STRING(OID_GEN_MAXIMUM_TOTAL_SIZE)
    MAKE_CASE_TO_STRING(OID_GEN_LINK_PARAMETERS)
    MAKE_CASE_TO_STRING(OID_GEN_INTERRUPT_MODERATION)
    MAKE_CASE_TO_STRING(OID_802_3_PERMANENT_ADDRESS)
    MAKE_CASE_TO_STRING(OID_802_3_CURRENT_ADDRESS)
    MAKE_CASE_TO_STRING(OID_802_3_MULTICAST_LIST)
    MAKE_CASE_TO_STRING(OID_802_3_MAXIMUM_LIST_SIZE)
    MAKE_CASE_TO_STRING(OID_PNP_SET_POWER)
    MAKE_CASE_TO_STRING(OID_PNP_QUERY_POWER)
    MAKE_CASE_TO_STRING(OID_IP4_OFFLOAD_STATS)
    MAKE_CASE_TO_STRING(OID_OFFLOAD_ENCAPSULATION)
    MAKE_CASE_TO_STRING(OID_TCP_OFFLOAD_PARAMETERS)
    MAKE_CASE_TO_STRING(OID_GEN_RECEIVE_SCALE_PARAMETERS)
    MAKE_CASE_TO_STRING(OID_GEN_RECEIVE_HASH)
    default:
      return nullptr;
  }
#undef MAKE_CASE_TO_STRING
}

// Validate request from OID_OFFLOAD_ENCAPSULATION.
bool ValidateOffloadEncapsulationRequest(
    const NDIS_OFFLOAD_ENCAPSULATION& encaps) {
  return encaps.Header.Revision == NDIS_OFFLOAD_ENCAPSULATION_REVISION_1 &&
         encaps.Header.Type == NDIS_OBJECT_TYPE_OFFLOAD_ENCAPSULATION &&
         (encaps.IPv4.Enabled != NDIS_OFFLOAD_SET_ON ||
          (encaps.IPv4.EncapsulationType & NDIS_ENCAPSULATION_IEEE_802_3)) &&
         (encaps.IPv4.Enabled != NDIS_OFFLOAD_SET_ON ||
          (encaps.IPv4.EncapsulationType & NDIS_ENCAPSULATION_IEEE_802_3));
}

NDIS_STATUS FillOidQueryInformation(NDIS_OID_REQUEST* oid_request,
                                    const void* reply, size_t size) {
  PAGED_CODE();
  oid_request->DATA.QUERY_INFORMATION.BytesNeeded = static_cast<UINT32>(size);
  if (oid_request->DATA.QUERY_INFORMATION.InformationBufferLength < size) {
    return NDIS_STATUS_BUFFER_TOO_SHORT;
  }

  NdisMoveMemory(oid_request->DATA.QUERY_INFORMATION.InformationBuffer, reply,
                 size);
  oid_request->DATA.QUERY_INFORMATION.BytesWritten = static_cast<UINT32>(size);
  return NDIS_STATUS_SUCCESS;
}

// Copy the SET_INFORMATION.InformationBuffer into memory location dest points
// to.
//
// Return:
//  NDIS_STATUS_BUFFER_TOO_SHORT if InformationBufferLength < size.
//  NDIS_STATUS_BUFFER_OVERFLOW if InformationBufferLength > size.
//  NDIS_STATUS_SUCCESS if copy succeeds.
NDIS_STATUS CopyOidInformation(NDIS_OID_REQUEST* oid_request, void* dest,
                               size_t size) {
  PAGED_CODE();
  NDIS_STATUS status = NDIS_STATUS_SUCCESS;
  UINT32 buffer_lengh =
      oid_request->DATA.SET_INFORMATION.InformationBufferLength;
  if (buffer_lengh == size) {
    oid_request->DATA.SET_INFORMATION.BytesRead = static_cast<UINT32>(size);
    NdisMoveMemory(dest, oid_request->DATA.SET_INFORMATION.InformationBuffer,
                   size);
  } else {
    oid_request->DATA.SET_INFORMATION.BytesRead = 0;
    oid_request->DATA.SET_INFORMATION.BytesNeeded = static_cast<UINT32>(size);
    if (buffer_lengh < size) {
      status = NDIS_STATUS_BUFFER_TOO_SHORT;
    } else {
      status = NDIS_STATUS_BUFFER_OVERFLOW;
    }
  }

  return status;
}

NDIS_STATUS HandleOidQuery(const AdapterContext& context,
                           NDIS_OID_REQUEST* oid_request) {
  PAGED_CODE();
  DEBUGP(GVNIC_VERBOSE, "---> HandleOidQuery");
  NDIS_STATUS status = NDIS_STATUS_SUCCESS;
  switch (oid_request->DATA.QUERY_INFORMATION.Oid) {
    case OID_GEN_STATISTICS: {
      const NDIS_STATISTICS_INFO& info = context.statistics.info();
      status = FillOidQueryInformation(oid_request, &info,
                                       sizeof(NDIS_STATISTICS_INFO));
      break;
    }
    case OID_GEN_XMIT_OK: {
      // The number of frames that have been transmitted without errors.

      UINT64 xmit_pkts = context.statistics.GetTransmitPacketCount();
      status = FillOidQueryInformation(oid_request, &xmit_pkts, sizeof(UINT64));
      break;
    }
    case OID_GEN_RCV_OK: {
      // The number of frames that the NIC receives without errors and indicates
      // to bound protocols.

      UINT64 rcv_pkts = context.statistics.GetReceivePacketCount();
      status = FillOidQueryInformation(oid_request, &rcv_pkts, sizeof(UINT64));
      break;
    }
    case OID_GEN_TRANSMIT_BUFFER_SPACE: {
      // The amount of memory, in bytes, on the NIC that is available for
      // buffering transmit data.

      const QueueConfig& tx_queue = context.device.transmit_queue_config();
      UINT32 tx_buffer_size = tx_queue.num_slices * tx_queue.num_traffic_class *
                              tx_queue.pages_per_queue_page_list * PAGE_SIZE;
      status =
          FillOidQueryInformation(oid_request, &tx_buffer_size, sizeof(UINT32));
      break;
    }
    case OID_GEN_RECEIVE_BUFFER_SPACE: {
      // The amount of memory on the NIC that is available for buffering receive
      // data.

      const QueueConfig& rx_queue = context.device.receive_queue_config();
      UINT32 rx_buffer_size = rx_queue.num_slices * rx_queue.num_traffic_class *
                              rx_queue.pages_per_queue_page_list * PAGE_SIZE;
      status =
          FillOidQueryInformation(oid_request, &rx_buffer_size, sizeof(UINT32));
      break;
    }
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
      // Number of bytes that a single net packet occupies in the transmit
      // buffer space of the NIC.
      __fallthrough;
    case OID_GEN_CURRENT_LOOKAHEAD:
      // The number of bytes of received packet data that will be indicated to
      // the protocol driver. This value is identical to that returned for
      // OID_GEN_RECEIVE_BLOCK_SIZE.
      __fallthrough;
    case OID_GEN_RECEIVE_BLOCK_SIZE:
      // The amount of storage, in bytes, that a single packet occupies in the
      // receive buffer space of the NIC.
      __fallthrough;
    case OID_GEN_MAXIMUM_TOTAL_SIZE: {
      // The maximum total packet length, in bytes, the NIC supports. This
      // specification includes the header.

      status = FillOidQueryInformation(
          oid_request, &context.device.max_packet_size().max_full_size,
          sizeof(UINT32));
      break;
    }
    case OID_GEN_VENDOR_ID: {
      status = FillOidQueryInformation(oid_request, &kVendorId, sizeof(UINT32));
      break;
    }
    case OID_GEN_VENDOR_DESCRIPTION: {
      status = FillOidQueryInformation(oid_request, kVendorName,
                                       sizeof(kVendorName));
      break;
    }
    case OID_GEN_VENDOR_DRIVER_VERSION: {
      // The low-order half of the return value specifies the minor version;
      // the high-order half specifies the major version.

      UINT version_number = MAJOR_DRIVER_VERSION << 16 | MINOR_DRIVER_VERSION;
      status =
          FillOidQueryInformation(oid_request, &version_number, sizeof(UINT32));
      break;
    }
    case OID_802_3_PERMANENT_ADDRESS: {
      status = FillOidQueryInformation(
          oid_request, context.device.permanent_mac_address(), kEthAddrLen);
      break;
    }
    case OID_802_3_CURRENT_ADDRESS: {
      status = FillOidQueryInformation(
          oid_request, context.device.current_mac_address(), kEthAddrLen);
      break;
    }
    case OID_PNP_QUERY_POWER: {
      // Whether it can transition its network adapter to the low-power state
      // specified in the InformationBuffer.

      status = NDIS_STATUS_SUCCESS;
      break;
    }
    case OID_GEN_CURRENT_PACKET_FILTER: {
      ULONG packet_filter = context.device.packet_filter();
      DEBUGP(GVNIC_VERBOSE, "return packet filter %lx", packet_filter);
      status = FillOidQueryInformation(
          oid_request, &packet_filter, sizeof(packet_filter));
      break;
    }
    case OID_GEN_INTERRUPT_MODERATION: {
      // To determine if interrupt moderation is enabled on a miniport adapter.
      // The feature is not currently support by gvnic.
      NDIS_INTERRUPT_MODERATION_PARAMETERS interrupt_moderation;
      interrupt_moderation.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
      interrupt_moderation.Header.Revision =
          NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
      interrupt_moderation.Header.Size =
          NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
      interrupt_moderation.Flags = 0;
      interrupt_moderation.InterruptModeration =
          NdisInterruptModerationNotSupported;
      status = FillOidQueryInformation(oid_request, &interrupt_moderation,
                                       sizeof(interrupt_moderation));
    }

    default: {
      status = STATUS_NDIS_NOT_SUPPORTED;
      break;
    }
  }

  DEBUGP(GVNIC_VERBOSE, "<--- HandleOidQuery");
  return status;
}

NDIS_STATUS HandleOidSet(AdapterContext* context,
                         NDIS_OID_REQUEST* oid_request) {
  PAGED_CODE();
  DEBUGP(GVNIC_VERBOSE, "---> HandleOidSet");
  NDIS_STATUS status = NDIS_STATUS_SUCCESS;

  switch (oid_request->DATA.SET_INFORMATION.Oid) {
    case OID_GEN_CURRENT_LOOKAHEAD:
      // The number of bytes of received packet data that the miniport driver
      // should indicate to the protocol driver.
      // This is a suggested value and driver is not required to follow it.
      // Follow netkvm logic by just ignoring this.
      status = NDIS_STATUS_SUCCESS;
      break;
    case OID_GEN_LINK_PARAMETERS: {
      // Set the current link state of a miniport adapter.
      // Currently, we cannot adjust link speed from driver side.
      NDIS_LINK_PARAMETERS link_params;
      status =
          CopyOidInformation(oid_request, &link_params, sizeof(link_params));
      if (status == NDIS_STATUS_SUCCESS) {
        DEBUGP(
            GVNIC_VERBOSE,
            "[%s] Requested Link Param: XmitLinkSpeed %llu, RcvLinkSpeed %llu, "
            "PauseFunction %#x, MediaDuplexState %#x, AutoNegotiationFlags %#x",
            __FUNCTION__, link_params.XmitLinkSpeed, link_params.RcvLinkSpeed,
            link_params.PauseFunctions, link_params.MediaDuplexState,
            link_params.AutoNegotiationFlags);
        status = NDIS_STATUS_NOT_ACCEPTED;
      }
      break;
    }
    case OID_GEN_INTERRUPT_MODERATION: {
      // Enable or disable the interrupt moderation on a miniport adapter.
      // Currently, gvnic doesn't support this feature.
      status = NDIS_STATUS_INVALID_DATA;
      break;
    }
    case OID_GEN_CURRENT_PACKET_FILTER: {
      // Specifies the types of net packets for which a protocol receives
      // indications from a miniport driver.
      ULONG packet_filter;
      status = CopyOidInformation(
          oid_request, &packet_filter, sizeof(packet_filter));
      if (status == NDIS_STATUS_SUCCESS) {
        context->device.set_packet_filter(packet_filter);
      }
      break;
    }
    case OID_802_3_MULTICAST_LIST: {
      // The gvnic device does not support multicast addresses, so don't reply
      // with a multicast list.
      status = NDIS_STATUS_NOT_SUPPORTED;
      break;
    }
    case OID_802_3_MAXIMUM_LIST_SIZE: {
      // The gvnic device does not support multicast addresses, so don't reply
      // with a size of the multicast list.
      status = NDIS_STATUS_NOT_SUPPORTED;
      break;
    }
    case OID_PNP_SET_POWER: {
      // Notifies a miniport driver that its underlying network adapter will be
      // transitioning to the device power state specified in the
      // InformationBuffer.
      status = NDIS_STATUS_NOT_ACCEPTED;
      break;
    }
    case OID_TCP_OFFLOAD_PARAMETERS: {
      // Sets the current TCP offload configuration of a miniport adapter.
      NDIS_OFFLOAD_PARAMETERS offload_params;
      status = CopyOidInformation(oid_request, &offload_params,
                                  sizeof(offload_params));
      if (status == NDIS_STATUS_SUCCESS) {
        status = context->device.UpdateOffloadConfig(offload_params);
      }
      break;
    }
    case OID_OFFLOAD_ENCAPSULATION: {
      // Set the task offload encapsulation settings of an underlying miniport
      // adapter.
      NDIS_OFFLOAD_ENCAPSULATION encaps;
      status = CopyOidInformation(oid_request, &encaps, sizeof(encaps));
      if (status == NDIS_STATUS_SUCCESS) {
        // Validate request.
        if (!ValidateOffloadEncapsulationRequest(encaps)) {
          status = NDIS_STATUS_INVALID_PARAMETER;
        } else {
          status = context->device.UpdateOffloadConfig(encaps);
        }
      }
      break;
    }
    case OID_GEN_RECEIVE_SCALE_PARAMETERS: {
      // This OID contains data outside the param structure and is consumed
      // through offset from the pointer. So raw pointer is passed in and
      // all validation is done inside the function body.
      status = context->device.UpdateRssParameters(
          reinterpret_cast<NDIS_RECEIVE_SCALE_PARAMETERS*>(
              oid_request->DATA.SET_INFORMATION.InformationBuffer),
          oid_request->DATA.SET_INFORMATION.InformationBufferLength,
          &oid_request->DATA.SET_INFORMATION.BytesRead);
      break;
    }
    default: {
      status = STATUS_NDIS_NOT_SUPPORTED;
      break;
    }
  }

  DEBUGP(GVNIC_VERBOSE, "<--- HandleOidSet");
  return status;
}
}  //  namespace

// Callback to handle an OID request to query or set information in the driver.
// Arguments:
//   miniport_adapter_context - A handle to a context area that the miniport
//    driver allocated in its MiniportInitializeEx function.
//   ndis_oid_request - A pointer to an NDIS_OID_REQUEST structure that contains
//    both the buffer and the request packet for the miniport driver to handle.
NDIS_STATUS GvnicOidRequest(NDIS_HANDLE miniport_adapter_context,
                            PNDIS_OID_REQUEST ndis_oid_request) {
  PAGED_CODE();
  DEBUGP(GVNIC_VERBOSE, "---> GvnicOidRequest");
  auto* context = static_cast<AdapterContext*>(miniport_adapter_context);
  NDIS_OID oid = ndis_oid_request->DATA.QUERY_INFORMATION.Oid;
  DEBUGP(GVNIC_VERBOSE, "Request oid: %d - %#x(%s)",
         ndis_oid_request->RequestType, oid, GetOidName(oid));

  NDIS_STATUS status = NDIS_STATUS_SUCCESS;

  switch (ndis_oid_request->RequestType) {
    case NdisRequestQueryInformation:
      __fallthrough;  // Pass through intentionally.
    case NdisRequestQueryStatistics:
      status = HandleOidQuery(*context, ndis_oid_request);
      break;
    case NdisRequestSetInformation:
      status = HandleOidSet(context, ndis_oid_request);
      break;
    default:
      status = NDIS_STATUS_NOT_SUPPORTED;
      break;
  }

  DEBUGP(GVNIC_VERBOSE, "<--- GvnicOidRequest %#x", status);
  return status;
}

// Callback to cancel an OID request.
// Arguments:
//   miniport_adapter_context - A handle to a context area that the miniport
//    driver allocated in its MiniportInitializeEx function.
//   request_id - A cancellation identifier for the request.
VOID GvnicOidCancelRequest(NDIS_HANDLE hminiport_adapter_context,
                           PVOID request_id) {
  UNREFERENCED_PARAMETER(hminiport_adapter_context);
  UNREFERENCED_PARAMETER(request_id);
}

#if NDIS_SUPPORT_NDIS61
// Callback to handle a direct OID request to query or set information.
// Arguments:
//   miniport_adapter_context - A handle to a context area that the miniport
//    driver allocated in its MiniportInitializeEx function.
//   ndis_oid_request - A pointer to an NDIS_OID_REQUEST structure that contains
//    both the buffer and the request packet for the miniport driver to handle.
NDIS_STATUS GvnicDirectOidRequest(NDIS_HANDLE miniport_adapter_context,
                                  PNDIS_OID_REQUEST ndis_oid_request) {
  NDIS_STATUS status = NDIS_STATUS_NOT_SUPPORTED;
  UNREFERENCED_PARAMETER(miniport_adapter_context);
  UNREFERENCED_PARAMETER(ndis_oid_request);
  return status;
}

// Callback to cancel a direct OID request.
// Arguments:
//   miniport_adapter_context - A handle to a context area that the miniport
//    driver allocated in its MiniportInitializeEx function.
//   request_id - A cancellation identifier for the request.
VOID GvnicCancelDirectOidRequest(NDIS_HANDLE miniport_adapter_context,
                                 PVOID request_id) {
  UNREFERENCED_PARAMETER(miniport_adapter_context);
  UNREFERENCED_PARAMETER(request_id);
}
#endif  // NDIS_SUPPORT_NDIS61
