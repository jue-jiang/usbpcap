/*
 *  Copyright (c) 2013 Tomasz Moń <desowin@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses>.
 */

#include "USBPcapMain.h"
#include "USBPcapURB.h"
#include "USBPcapTables.h"

#include <stddef.h> /* Required for offsetof macro */

BOOLEAN USBPcapRetrieveEndpointInfo(IN PROOTHUB_DATA pRootHub,
                                    IN USBD_PIPE_HANDLE handle,
                                    PUSBPCAP_ENDPOINT_INFO pInfo)
{
    KIRQL irql;
    PUSBPCAP_ENDPOINT_INFO info;
    BOOLEAN found = FALSE;

    KeAcquireSpinLock(&pRootHub->endpointTableSpinLock, &irql);
    info = USBPcapGetEndpointInfo(pRootHub->endpointTable, handle);
    if (info != NULL)
    {
        found = TRUE;
        memcpy(pInfo, info, sizeof(USBPCAP_ENDPOINT_INFO));
    }
    KeReleaseSpinLock(&pRootHub->endpointTableSpinLock, irql);

    if (found == TRUE)
    {
        DkDbgVal("Found endpoint info", handle);
        DkDbgVal("", pInfo->type);
        DkDbgVal("", pInfo->endpointAddress);
        DkDbgVal("", pInfo->deviceAddress);
    }
    else
    {
        DkDbgVal("Unable to find endpoint info", handle);
    }

    return found;
}

#if DBG
VOID USBPcapPrintChars(PCHAR text, PUCHAR buffer, ULONG length)
{
    ULONG i;

    KdPrint(("%s HEX: ", text));
    for (i = 0; i < length; ++i)
    {
        KdPrint(("%02X ", buffer[i]));
    }

    KdPrint(("\n%s TEXT: ", text));
    for (i = 0; i < length; ++i)
    {
        /*
         * For printable characters, print the character,
         * otherwise print dot
         */
        if (buffer[i] >= 0x20 && buffer[i] <= 0x7E)
        {
            KdPrint(("%c", buffer[i]));
        }
        else
        {
            KdPrint(("."));
        }
    }

    KdPrint(("\n"));
}
#else
#define USBPcapPrintChars(text, buffer, length) {}
#endif

/*
 * Analyzes the URB
 *
 * post is FALSE when the request is being on its way to the bus driver
 * post is TRUE when the request returns from the bus driver
 */
VOID USBPcapAnalyzeURB(PURB pUrb, BOOLEAN post, PROOTHUB_DATA pRootHub)
{
    struct _URB_HEADER *header;

    ASSERT(pUrb != NULL);
    ASSERT(pRootHub != NULL);

    header = (struct _URB_HEADER*)pUrb;

    switch (header->Function)
    {
        case URB_FUNCTION_SELECT_CONFIGURATION:
        {
            ULONG i, j;
            USHORT interfaces_len;
            struct _URB_SELECT_CONFIGURATION *pSelectConfiguration;
            PUSBD_INTERFACE_INFORMATION pInterface;
            UCHAR deviceAddress;
            KIRQL irql;

            if (post == FALSE)
            {
                /* Pass the request to host controller,
                * we are interested only in select configuration
                * after the fields are set by host controller driver */
                break;
            }

            /* FIXME: Get device address */
            deviceAddress = 255;

            pSelectConfiguration = (struct _URB_SELECT_CONFIGURATION*)pUrb;
            pInterface = &pSelectConfiguration->Interface;
            DkDbgStr("SELECT_CONFIGURATION");

            /* Calculate interfaces length */
            interfaces_len = pUrb->UrbHeader.Length;
            interfaces_len -= offsetof(struct _URB_SELECT_CONFIGURATION, Interface);

            KdPrint(("Header Len: %d Interfaces_len: %d\n",
                    pUrb->UrbHeader.Length, interfaces_len));

            i = 0;
            /* Iterate over all interfaces in search for pipe handles */
            while (interfaces_len > 0)
            {
                PUSBD_PIPE_INFORMATION Pipe = pInterface->Pipes;
                KdPrint(("Interface %d Len: %d Class: %02x Subclass: %02x"
                         "Protocol: %02x Number of Pipes: %d\n",
                         i, pInterface->Length, pInterface->Class,
                         pInterface->SubClass, pInterface->Protocol,
                         pInterface->NumberOfPipes));

                for (j=0; j<pInterface->NumberOfPipes; ++j, Pipe++)
                {
                    KdPrint(("Pipe %d MaxPacketSize: %d"
                            "EndpointAddress: %d PipeType: %d"
                            "PipeHandle: %02x\n",
                            j,
                            Pipe->MaximumPacketSize,
                            Pipe->EndpointAddress,
                            Pipe->PipeType,
                            Pipe->PipeHandle));

                    KeAcquireSpinLock(&pRootHub->endpointTableSpinLock,
                                      &irql);
                    USBPcapAddEndpointInfo(pRootHub->endpointTable,
                                           Pipe,
                                           deviceAddress);
                    KeReleaseSpinLock(&pRootHub->endpointTableSpinLock,
                                      irql);
                }

                /* Advance to next interface */
                i++;
                interfaces_len -= pInterface->Length;
                pInterface = (PUSBD_INTERFACE_INFORMATION)
                                 ((PUCHAR)pInterface + pInterface->Length);
            }
            break;
        }

        case URB_FUNCTION_CONTROL_TRANSFER:
        {
            struct _URB_CONTROL_TRANSFER* transfer;

            transfer = (struct _URB_CONTROL_TRANSFER*)pUrb;

            DkDbgStr("URB_FUNCTION_CONTROL_TRANSFER");

            DkDbgVal("", transfer->PipeHandle);
            USBPcapPrintChars("Setup Packet", &transfer->SetupPacket[0], 8);
            if (transfer->TransferBuffer != NULL)
            {
                USBPcapPrintChars("Transfer Buffer",
                                 transfer->TransferBuffer,
                                 transfer->TransferBufferLength);
            }
            break;
        }

#if (_WIN32_WINNT >= 0x0600)
        case URB_FUNCTION_CONTROL_TRANSFER_EX:
        {
            struct _URB_CONTROL_TRANSFER_EX* transfer;

            transfer = (struct _URB_CONTROL_TRANSFER_EX*)pUrb;

            DkDbgStr("URB_FUNCTION_CONTROL_TRANSFER");

            DkDbgVal("", transfer->PipeHandle);
            USBPcapPrintChars("Setup Packet", &transfer->SetupPacket[0], 8);
            if (transfer->TransferBuffer != NULL)
            {
                USBPcapPrintChars("Transfer Buffer",
                                  transfer->TransferBuffer,
                                  transfer->TransferBufferLength);
            }
            break;
        }
#endif

        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
        {
            struct _URB_BULK_OR_INTERRUPT_TRANSFER  *transfer;
            USBPCAP_ENDPOINT_INFO                   info;
            BOOLEAN                                 epFound;

            transfer = (struct _URB_BULK_OR_INTERRUPT_TRANSFER*)pUrb;

            DkDbgStr("URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER");
            DkDbgVal("", transfer->PipeHandle);
            epFound = USBPcapRetrieveEndpointInfo(pRootHub,
                                                  transfer->PipeHandle,
                                                  &info);
            DkDbgVal("", transfer->TransferFlags);
            DkDbgVal("", transfer->TransferBufferLength);
            DkDbgVal("", transfer->TransferBuffer);
            DkDbgVal("", transfer->TransferBufferMDL);
            if (transfer->TransferBuffer != NULL)
            {
                USBPcapPrintChars("Transfer Buffer",
                                  transfer->TransferBuffer,
                                  transfer->TransferBufferLength);
            }
            break;
        }

        case URB_FUNCTION_ISOCH_TRANSFER:
        {
            struct _URB_ISOCH_TRANSFER *transfer;

            transfer = (struct _URB_ISOCH_TRANSFER*)pUrb;

            DkDbgStr("URB_FUNCTION_ISOCH_TRANSFER");
            DkDbgVal("", transfer->PipeHandle);
            DkDbgVal("", transfer->TransferFlags);
            DkDbgVal("", transfer->NumberOfPackets);
            break;
        }

        case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
        case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
        case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
        {
            struct _URB_CONTROL_DESCRIPTOR_REQUEST *request;

            request = (struct _URB_CONTROL_DESCRIPTOR_REQUEST*)pUrb;

            DkDbgVal("URB_CONTROL_DESCRIPTOR_REQUEST", header->Function);

            DkDbgVal("", request->TransferBufferLength);
            DkDbgVal("", request->TransferBuffer);
            DkDbgVal("", request->TransferBufferMDL);
            switch (request->DescriptorType)
            {
                case USB_DEVICE_DESCRIPTOR_TYPE:
                    DkDbgVal("USB_DEVICE_DESCRIPTOR_TYPE",
                             request->DescriptorType);
                    break;
                case USB_CONFIGURATION_DESCRIPTOR_TYPE:
                    DkDbgVal("USB_CONFIGURATION_DESCRIPTOR_TYPE",
                             request->DescriptorType);
                    break;
                case USB_STRING_DESCRIPTOR_TYPE:
                    DkDbgVal("USB_STRING_DESCRIPTOR_TYPE",
                             request->DescriptorType);
                    break;
                default:
                    break;
            }
            DkDbgVal("", request->LanguageId);

            if (request->TransferBuffer != NULL)
            {
                USBPcapPrintChars("Transfer Buffer",
                                  request->TransferBuffer,
                                  request->TransferBufferLength);
            }

            break;
        }

        default:
            DkDbgVal("Unknown URB type", header->Function);
    }
}