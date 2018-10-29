#pragma once
#include "../vRt_internal.hpp"
// planned after RTX 2070 or after 2019 year


// 
namespace _vt {
    class RTXAcceleratorSetExtension;
    class RTXAccelerator;
};


// TODO Turing
namespace vrt {

    // extending to enum type
    constexpr inline static const auto VT_ACCELERATION_NAME_RTX = VtAccelerationName(0x00001000u); // planned in 2019
    class VtRTXAccelerationExtension; // structure extension

};


// lower level hard classes (WIP)
#include "vRt/Backland/Definitions/HardClasses.inl"
namespace _vt {

    // 
    class RTXAcceleratorSetExtension : public AcceleratorSetExtensionBase, std::enable_shared_from_this<RTXAcceleratorSetExtension> {
    public:
        friend Device;
        friend AcceleratorSetExtensionBase;
        virtual VtAccelerationName _AccelerationName() const override { return VT_ACCELERATION_NAME_RTX; };

    };

    // 
    class RTXAccelerator : public AdvancedAcceleratorBase, std::enable_shared_from_this<RTXAccelerator> {
    public:
        friend Device;
        friend AdvancedAcceleratorBase;
        virtual VtAccelerationName _AccelerationName() const override { return VT_ACCELERATION_NAME_RTX; };

    };

};


#include "vRt/Backland/Definitions/Structures.inl" // required structures for this part
namespace vrt {
    // passing structure
    class VtRTXAccelerationExtension : public VtDeviceAdvancedAccelerationExtension {
    public:
        friend VtDeviceAdvancedAccelerationExtension;
        virtual VtAccelerationName _AccelerationName() const override { return VT_ACCELERATION_NAME_RTX; };


    };
};
