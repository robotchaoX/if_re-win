/*++

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
    ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
    THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
    PARTICULAR PURPOSE.

    Copyright (c) Microsoft Corporation. All rights reserved

--*/

#include "precomp.h"

#include "trace.h"
#include "configuration.h"
#include "adapter.h"

typedef struct _RT_ADVANCED_PROPERTY
{
    NDIS_STRING RegName;  // variable name text
    UINT FieldOffset;     // offset to RT_ADAPTER field
    UINT FieldSize;       // size (in bytes) of the field
    UINT Default;         // default value to use
    UINT Min;             // minimum value allowed
    UINT Max;             // maximum value allowed
} RT_ADVANCED_PROPERTY;

#define RT_OFFSET(field)   ((UINT)FIELD_OFFSET(RT_ADAPTER,field))
#define RT_SIZE(field)     sizeof(((RT_ADAPTER *)0)->field)

RT_ADVANCED_PROPERTY RtSupportedProperties[] =
{
    // reg value name                                Offset in RT_ADAPTER                 Field size                         Default Value                     Min                               Max

    // Standard Keywords
    { NDIS_STRING_CONST("*SpeedDuplex"),             RT_OFFSET(SpeedDuplex),              RT_SIZE(SpeedDuplex),              RtSpeedDuplexModeAutoNegotiation, RtSpeedDuplexModeAutoNegotiation, RtSpeedDuplexMode1GFullDuplex },
    { NDIS_STRING_CONST("*FlowControl"),             RT_OFFSET(FlowControl),              RT_SIZE(FlowControl),              FlowControlTxRx,                  NoFlowControl,                    FlowControlTxRx },
    { NDIS_STRING_CONST("*RegVlanid"),               RT_OFFSET(VlanID),                   RT_SIZE(VlanID),                   0,                                0,                                0xFFF },
    { NDIS_STRING_CONST("*EEE"),                     RT_OFFSET(EEEEnable),                RT_SIZE(EEEEnable),                TRUE,                             FALSE,                            TRUE }
};

NTSTATUS
RtAdapterReadConfiguration(
    _In_ RT_ADAPTER* adapter)
    /*++
    Routine Description:

        Read the following from the registry
        1. All the parameters
        2. NetworkAddres

    Arguments:

        adapter                         Pointer to our adapter

    Return Value:

        STATUS_SUCCESS
        STATUS_INSUFFICIENT_RESOURCES

    --*/
{
    TraceEntryRtAdapter(adapter);

    NTSTATUS status = STATUS_SUCCESS;

    NETCONFIGURATION configuration;
    GOTO_IF_NOT_NT_SUCCESS(Exit, status,
        NetAdapterOpenConfiguration(adapter->NetAdapter, WDF_NO_OBJECT_ATTRIBUTES, &configuration));

    // read all the registry values
    for (UINT i = 0; i < ARRAYSIZE(RtSupportedProperties); i++)
    {
        RT_ADVANCED_PROPERTY* property = &RtSupportedProperties[i];

        // Driver should NOT fail the initialization only because it can not
        // read the registry
        PUCHAR pointer = (PUCHAR)adapter + property->FieldOffset;

        // Get the configuration value for a specific parameter.  Under NT the
        // parameters are all read in as DWORDs.
        ULONG value = 0;
        status = NetConfigurationQueryUlong(
            configuration,
            NET_CONFIGURATION_QUERY_ULONG_NO_FLAGS,
            &property->RegName,
            &value);

        // If the parameter was present, then check its value for validity.
        if (NT_SUCCESS(status))
        {
            // Check that param value is not too small or too large

            if (value < property->Min ||
                value > property->Max)
            {
                value = property->Default;
            }
        }
        else
        {
            value = property->Default;
            status = STATUS_SUCCESS;
        }

        TraceLoggingWrite(
            RealtekTraceProvider,
            "ReadConfiguration",
            TraceLoggingRtAdapter(adapter),
            TraceLoggingUnicodeString(&property->RegName, "Key"),
            TraceLoggingUInt32(value, "Value"));

        // Store the value in the adapter structure.
        switch (property->FieldSize)
        {
        case 1:
            *((PUCHAR)pointer) = (UCHAR)value;
            break;

        case 2:
            *((PUSHORT)pointer) = (USHORT)value;
            break;

        case 4:
            *((PULONG)pointer) = (ULONG)value;
            break;

        default:
            TraceLoggingWrite(
                RealtekTraceProvider,
                "InvalidFieldSize",
                TraceLoggingLevel(TRACE_LEVEL_ERROR),
                TraceLoggingRtAdapter(adapter),
                TraceLoggingUnicodeString(&property->RegName, "Key"),
                TraceLoggingUInt32(value, "Value"),
                TraceLoggingUInt32(property->FieldSize, "FieldSize"));
            break;
        }
    }

    // Read NetworkAddress registry value
    // Use it as the current address if any
    status = NetConfigurationQueryLinkLayerAddress(
        configuration,
        &adapter->CurrentAddress);

    if ((status == STATUS_SUCCESS))
    {
        if (adapter->CurrentAddress.Length != ETH_LENGTH_OF_ADDRESS ||
            ETH_IS_MULTICAST(adapter->CurrentAddress.Address) ||
            ETH_IS_BROADCAST(adapter->CurrentAddress.Address))
        {
            TraceLoggingWrite(
                RealtekTraceProvider,
                "InvalidNetworkAddress",
                TraceLoggingBinary(adapter->CurrentAddress.Address, adapter->CurrentAddress.Length));
        }
        else
        {
            adapter->OverrideAddress = TRUE;
        }
    }

    status = STATUS_SUCCESS;


Exit:
    if (configuration)
    {
        NetConfigurationClose(configuration);
    }

    TraceExitResult(status);
    return STATUS_SUCCESS;
}