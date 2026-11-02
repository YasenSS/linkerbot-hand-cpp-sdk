# Manual Connection

When the model, hand side, adapter, and device address are known, connect manually using `HandDeviceConfig`.
All complete examples only read current angles; replace the model and parameters with the actual values before use.
For build instructions, see [Prepare the communication interface](prepare-interface.md#build-and-link-prerequisites).

| Model | `Model` | Joint count | Default communication and address |
| --- | --- | ---: | --- |
| L6 | `kL6` | 6 | Classic CAN, left `0x28`, right `0x27` |
| O6 | `kO6` | 6 | Classic CAN, left `0x28`, right `0x27` |
| L20 Lite | `kL20Lite` | 10 | Classic CAN, left `0x28`, right `0x27` |
| L20 | `kL20` | 16 | Classic CAN, left `0x28`, right `0x27` |
| L30 | `kL30` | 17 | Extended FD, NodeID defaults to 1, HostID defaults to 0 |
| O30 | `kO30` | 20 | Standard FD, no BRS; HOP request ID defaults to `0x001` |

`interface_name` identifies an interface or connection, not a device CAN address. If you explicitly set `address` for Classic CAN,
it must match the fixed ID implied by the hand side; `host_address` is only used for L30.
C++ currently has no O20 model; see [Support status](../tutorials/o20.md).

<a id="socketcan"></a>

## SocketCAN (Linux)

First enable the actual interface as described in [Prepare the communication interface](prepare-interface.md). On Linux, when no factory is supplied, the SDK selects the default SocketCAN transport for the model.

### Classic CAN

The following connects to a left-hand L6. Change `model` for other Classic models; for a right hand, use `kRight` and `0x27`.

```cpp
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/adapter_transport.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

// Attempt to close the same device object after either success or failure, preserving the original exception.
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("Angle sample unavailable");
        }
        if (angles.meta.stale) {
            std::cerr << "Angle sample is stale\n";
        }
        for (const auto value : angles.raw) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        hand.close();
    } catch (const std::exception &error) {
        if (!failure) {
            throw;
        }
        std::cerr << "Failed to close connection: " << error.what() << '\n';
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

int main() try {
    HandDeviceConfig config;
    config.model = Model::kL6;
    config.side = HandSide::kLeft;
    config.interface_name = "can0";
    config.address = 0x28;
    HandDevice hand(config);
    read_and_close(hand);
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

### CAN FD

The following connects to a right-hand O30. For L30, change `model` to `Model::kL30` and set `address` to the actual NodeID;
use `host_address` for the actual HostID and set `side` to match the deployment. The two devices use different frame formats, so changing the address alone is insufficient.

```cpp
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/adapter_transport.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

// Attempt to close the same device object after either success or failure, preserving the original exception.
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("Angle sample unavailable");
        }
        if (angles.meta.stale) {
            std::cerr << "Angle sample is stale\n";
        }
        for (const auto value : angles.raw) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        hand.close();
    } catch (const std::exception &error) {
        if (!failure) {
            throw;
        }
        std::cerr << "Failed to close connection: " << error.what() << '\n';
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

int main() try {
    HandDeviceConfig config;
    config.model = Model::kO30;
    config.side = HandSide::kRight;
    config.interface_name = "can0";
    config.address = 0x001;
    HandDevice hand(config);
    read_and_close(hand);
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

For optional L6 HOP firmware, explicitly set `config.l6_protocol = L6Protocol::kHopV004`
and verify the corresponding address, bitrate, and capabilities. You cannot infer the device protocol from the FD interface configuration.
See [Optional HOP connection](../guides/usage.md#optional-l6-can-fd-protocol).

<a id="pcan-slcan"></a>

## PCAN and SLCAN (Windows)

Changing only `interface_name` does not automatically select an adapter. Supply the corresponding factory, or use [Automatic connection](automatic.md).

### PCAN Classic CAN

Set the library path, channel, and hand side for the actual deployment. The following connects to a left-hand L6:

```cpp
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/adapter_transport.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

// Attempt to close the same device object after either success or failure, preserving the original exception.
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("Angle sample unavailable");
        }
        if (angles.meta.stale) {
            std::cerr << "Angle sample is stale\n";
        }
        for (const auto value : angles.raw) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        hand.close();
    } catch (const std::exception &error) {
        if (!failure) {
            throw;
        }
        std::cerr << "Failed to close connection: " << error.what() << '\n';
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

int main() try {
    linkerbot::can::PcanConfig pcan;
    pcan.channel = linkerbot::can::pcan_channel_handle("PCAN_USBBUS1");
    pcan.library_path = "C:/vendor/PCANBasic.dll";
    pcan.nominal_bitrate = 1'000'000;
    HandDeviceConfig config;
    config.model = Model::kL6;
    config.side = HandSide::kLeft;
    config.interface_name = "pcan:PCAN_USBBUS1";
    config.address = 0x28;
    HandDevice hand(config, make_pcan_transport_factory(pcan));
    read_and_close(hand);
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

### PCAN FD

The following connects to L30. Use an FD-capable adapter with bit timing matching the actual bus. The default `PcanFdTiming`
corresponds to 1M/5M with an 80 MHz clock; this does not mean the hardware has been validated.

```cpp
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/adapter_transport.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

// Attempt to close the same device object after either success or failure, preserving the original exception.
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("Angle sample unavailable");
        }
        if (angles.meta.stale) {
            std::cerr << "Angle sample is stale\n";
        }
        for (const auto value : angles.raw) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        hand.close();
    } catch (const std::exception &error) {
        if (!failure) {
            throw;
        }
        std::cerr << "Failed to close connection: " << error.what() << '\n';
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

int main() try {
    linkerbot::can::PcanConfig pcan;
    pcan.channel = linkerbot::can::pcan_channel_handle("PCAN_USBBUS1");
    pcan.library_path = "C:/vendor/PCANBasic.dll";
    pcan.mode = linkerbot::can::PcanMode::kFd;
    pcan.nominal_bitrate = 1'000'000;
    pcan.data_bitrate = 5'000'000;
    pcan.fd_timing = linkerbot::can::PcanFdTiming{};
    HandDeviceConfig config;
    config.model = Model::kL30;
    config.side = HandSide::kLeft;
    config.interface_name = "pcan:PCAN_USBBUS1";
    config.address = 1;
    config.host_address = 0;
    HandDevice hand(config, make_pcan_transport_factory(pcan));
    read_and_close(hand);
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

### SLCAN CAN FD

This example connects to O30 using firmware compatible with the CANable FD extension. Replace `COM5`, the hand side, and the address with the actual values.
`serial_baud` is the serial baud rate; `nominal_bitrate` and `data_bitrate` are bus bitrates.

```cpp
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/adapter_transport.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

// Attempt to close the same device object after either success or failure, preserving the original exception.
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("Angle sample unavailable");
        }
        if (angles.meta.stale) {
            std::cerr << "Angle sample is stale\n";
        }
        for (const auto value : angles.raw) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        hand.close();
    } catch (const std::exception &error) {
        if (!failure) {
            throw;
        }
        std::cerr << "Failed to close connection: " << error.what() << '\n';
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

int main() try {
    linkerbot::can::SlcanConfig serial;
    serial.port = "COM5";
    serial.serial_baud = 115200;
    serial.protocol = linkerbot::can::SlcanProtocol::kCanableFd;
    serial.fd = true;
    serial.brs = false;
    serial.nominal_bitrate = 1'000'000;
    serial.data_bitrate = 5'000'000;
    HandDeviceConfig config;
    config.model = Model::kO30;
    config.side = HandSide::kRight;
    config.interface_name = "slcan:COM5";
    config.address = 0x001;
    HandDevice hand(config, make_slcan_transport_factory(serial));
    read_and_close(hand);
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

For Classic Lawicel firmware, use `SlcanProtocol::kLawicelClassic`, set `fd = false`, and select a Classic model.
CANable FD firmware can also carry Classic frames; set `fd = false` in that case as well.
The factory requires a compatible transport library with the SLCAN backend; the serial port is not automatically substituted based on VID/PID.

<a id="minicanfd"></a>

## MiniCANFD (Linux / Windows)

The following connects to O30. On Linux, use the actual device number, channel within the device, and `libcanbus.so` path;
on Windows, use the vendor channel number as `device_index`, set `channel_index = 0`, and supply the actual `HCanbus.dll` path.
Manual connection uses only the specified channel; it does not try other devices or channels.

```cpp
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <linkerbot/hand/discovery.hpp>
#include <linkerbot/hand/adapter_transport.hpp>

using namespace linkerbot::hand;
using namespace std::chrono_literals;

// Attempt to close the same device object after either success or failure, preserving the original exception.
void read_and_close(HandDevice &hand) {
    std::exception_ptr failure;
    try {
        hand.open();
        const auto angles = hand.get_angles(1s);
        if (!angles.meta.valid) {
            throw std::runtime_error("Angle sample unavailable");
        }
        if (angles.meta.stale) {
            std::cerr << "Angle sample is stale\n";
        }
        for (const auto value : angles.raw) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        hand.close();
    } catch (const std::exception &error) {
        if (!failure) {
            throw;
        }
        std::cerr << "Failed to close connection: " << error.what() << '\n';
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}

int main() try {
    linkerbot::can::VendorCanFdTransport::Config mini;
    mini.device_index = 0;
    mini.channel_index = 0;
    mini.library_path = "/opt/minicanfd/libcanbus.so";
    mini.nom_baud = 1'000'000;
    mini.dat_baud = 5'000'000;
    HandDeviceConfig config;
    config.model = Model::kO30;
    config.side = HandSide::kRight;
    config.interface_name = "minicanfd:0:0";
    config.address = 0x001;
    HandDevice hand(config, make_minicanfd_transport_factory(mini));
    read_and_close(hand);
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

## Opening, closing, and subsequent operations

Constructing a `HandDevice` or factory does not open the connection. Control is available only after `open()` verifies identity and baseline state;
call `close()` to end the connection. In each program above, `read_and_close()` attempts to close even if reading fails
and does not treat a failed read as success. If closing fails, a long-running program should retain the same object, resolve the fault, and retry closing.

Place feature code after `open()` succeeds and before `close()`; see
[L6](../tutorials/l6.md), [O6](../tutorials/o6.md), [L20 Lite](../tutorials/l20lite.md),
[L20](../tutorials/l20.md), [L30](../tutorials/l30.md), and [O30](../tutorials/o30.md).
Model-specific Managers usually acquire resources when the model object is constructed and use `stop()` when finished, unlike the unified API lifecycle;
do not let the two object types control the same device simultaneously.

[Back to the documentation overview](../README.md)
