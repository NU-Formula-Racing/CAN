#pragma once

#include <stdint.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <vector>

#ifdef TIMER_IMPLEMENTATION
#include TIMER_IMPLEMENTATION
#else
#include "virtualTimer.h"
#endif

#ifdef FREERTOS_ATOMIC_IMPL
#include "freertos_atomic.h"
#else
#define Atomic std::atomic
#endif

#ifdef ARDUINO
#include <Arduino.h>
#endif

class CANMessage
{
public:
    CANMessage(uint32_t id, bool extended_id, uint8_t len, std::array<uint8_t, 8> data)
        : id_{id}, extended_id_{extended_id}, len_{len}, data_{data}
    {
    }

    // default to standard id for backwards compatibility
    CANMessage(uint32_t id, uint8_t len, std::array<uint8_t, 8> data) : CANMessage(id, false, len, data) {}

    uint32_t id_;
    bool extended_id_;
    uint8_t len_;
    std::array<uint8_t, 8> data_;
};

/**
 * @brief PGNCANMessage is used to implement J1939 and NMEA2000
 *
 */
class PGNCANMessage : public CANMessage
{
public:
    union PGN
    {
        uint32_t raw;
        struct
        {
            uint8_t pdu_specific : 8;
            uint8_t pdu_format : 8;
            bool data_page : 1;
            bool reserved : 1;
        } pgn;
        PGN() = default;
        operator uint32_t() const { return raw; }
        PGN(uint32_t r) : raw{r} {}
        PGN(bool data_page, uint8_t pdu_format, uint8_t pdu_specific)
            : pgn{.pdu_specific = pdu_specific, .pdu_format = pdu_format, .data_page = data_page, .reserved = 0}
        {
        }
    };

    union ExtendedId
    {
        uint32_t raw;
        struct
        {
            uint8_t source_address : 8;
            uint32_t pgn : 18;
            uint8_t priority : 3;
        } extended_id;
        ExtendedId() = default;
        operator uint32_t() const { return raw; }
        ExtendedId(uint32_t r) : raw{r} {}
        ExtendedId(uint8_t source_address, PGN pgn, uint8_t priority)
            : extended_id{.source_address = source_address,
                          .pgn = static_cast<uint32_t>(pgn & 0x3FFFF),
                          .priority = static_cast<uint8_t>(priority & 0b11)

            }
        {
        }
    };
    PGNCANMessage(ExtendedId id, uint8_t len, std::array<uint8_t, 8> data) : CANMessage{id, true, len, data} {}
};

class ICANSignal
{
public:
    enum class ByteOrder
    {
        kBigEndian,
        kLittleEndian
    };
    virtual void EncodeSignal(uint64_t *buffer) = 0;
    virtual void DecodeSignal(uint64_t *buffer) = 0;
};

template <class T>
constexpr typename std::enable_if<std::is_unsigned<T>::value, T>::type bswap(T i, T j = 0u, std::size_t n = 0u)
{
    return n == sizeof(T) ? j : bswap<T>(i >> 8, (j << 8) | (i & (T)(unsigned char)(-1)), n + 1);
}

constexpr uint8_t CANSignal_generate_position(uint8_t position, uint8_t length, ICANSignal::ByteOrder byte_order)
{
    return static_cast<uint8_t>(
        (byte_order == ICANSignal::ByteOrder::kLittleEndian
         || (length - (8 - (position % 8)) /* bits_in_last_byte */ < 0))
            ? position
            : position
                  - ((8
                      * (((length - (8 - (position % 8)) /* bits_in_last_byte */) % 8) /* remaining_bits */ == 0
                             ? ((length - (8 - (position % 8)) /* bits_in_last_byte */) / 8) /* full_bytes */
                             : ((length - (8 - (position % 8)) /* bits_in_last_byte */) / 8) /* full_bytes */ + 1))
                     + (8 - ((length - (8 - (position % 8)) /* bits_in_last_byte */) % 8) /* remaining_bits */)
                     - (8 - (position % 8)) /* bits_in_last_byte */));
    /* //Expanded version because the nested ternary operators are incomprehensible, but they are required for C++11
        if (byte_order == ICANSignal::ByteOrder::kLittleEndian)
        {
            return position;
        }
        else
        {
            uint8_t bits_in_last_byte{8 - (position % 8)};
            if (length - bits_in_last_byte < 0)
            {
                return position;
            }
            else
            {
                uint8_t full_bytes = (length - bits_in_last_byte) / 8;
                uint8_t remaining_bits = (length - bits_in_last_byte) % 8;
                position -=
                    (8 * (remaining_bits == 0 ? full_bytes : full_bytes + 1)) + (8 - remaining_bits) -
       bits_in_last_byte;
            }

            return position;
        } */
}

// Generates a mask of which bits in the message correspond to a specific signal
constexpr uint64_t CANSignal_generate_mask(uint8_t position, uint8_t length, ICANSignal::ByteOrder byte_order)
{
    return (byte_order == ICANSignal::ByteOrder::kLittleEndian)
               ? (0xFFFFFFFFFFFFFFFFull << (64 - length) >> (64 - (length + position)))
               : (bswap((uint64_t)(0xFFFFFFFFFFFFFFFFull >> (64 - length) << (64 - (length + position)))));
}

template <typename SignalType>
class ITypedCANSignal : public ICANSignal
{
public:
    Atomic<SignalType> &value_ref() { return signal_; }

    operator SignalType() const { return signal_; }

    void operator=(const SignalType &signal) { signal_ = signal; }

    SignalType operator+=(const SignalType &signal) { return signal_ = signal_ + signal; }

    SignalType operator-=(const SignalType &signal) { return signal_ = signal_ - signal; }

    SignalType operator*=(const SignalType &signal) { return signal_ = signal_ * signal; }

    SignalType operator/=(const SignalType &signal) { return signal_ = signal_ / signal; }

    bool operator>(const SignalType &signal) { return signal_ > signal; }

    bool operator<(const SignalType &signal) { return signal_ < signal; }

    bool operator>=(const SignalType &signal) { return signal_ >= signal; }

    bool operator<=(const SignalType &signal) { return signal_ <= signal; }

    void operator=(const ITypedCANSignal<SignalType> &signal) { signal_ = signal; }

    SignalType operator+=(const ITypedCANSignal<SignalType> &signal) { return signal_ = signal_ + signal; }

    SignalType operator-=(const ITypedCANSignal<SignalType> &signal) { return signal_ = signal_ - signal; }

    SignalType operator*=(const ITypedCANSignal<SignalType> &signal) { return signal_ = signal_ * signal; }

    SignalType operator/=(const ITypedCANSignal<SignalType> &signal) { return signal_ = signal_ / signal; }

    bool operator>(const ITypedCANSignal<SignalType> &signal) { return signal_ > signal; }

    bool operator<(const ITypedCANSignal<SignalType> &signal) { return signal_ < signal; }

    bool operator>=(const ITypedCANSignal<SignalType> &signal) { return signal_ >= signal; }

    bool operator<=(const ITypedCANSignal<SignalType> &signal) { return signal_ <= signal; }

protected:
    Atomic<SignalType> signal_;
};

// Needed so compiler knows these template classes exist
template class ITypedCANSignal<uint8_t>;
template class ITypedCANSignal<uint16_t>;
template class ITypedCANSignal<uint32_t>;
template class ITypedCANSignal<int8_t>;
template class ITypedCANSignal<int16_t>;
template class ITypedCANSignal<int32_t>;
template class ITypedCANSignal<float>;

static constexpr int kCANTemplateFloatDenominator{1 << 16};  // 2^16
constexpr int CANTemplateConvertFloat(float value) { return static_cast<int>(value * kCANTemplateFloatDenominator); }
constexpr float CANTemplateGetFloat(int value) { return static_cast<float>(value) / kCANTemplateFloatDenominator; }

template <bool signed_raw>
struct GetCANRawType;

template <>
struct GetCANRawType<true>
{
    using type = int64_t;
};

template <>
struct GetCANRawType<false>
{
    using type = uint64_t;
};

/**
 * @brief A class for decoding and encoding CAN signals. Note: only works with little endian signals on a little endian
 * architecture, and you must manually ensure consistency with the DBC
 *
 * @tparam SignalType The type of variable in the application to be encoded/decoded
 * @tparam position The position of the first bit of the signal in the message
 * @tparam length The length of the signal in the message
 * @tparam factor The factor to multiply the raw signal by (gotten using CANTemplateConvertFloat(float value))
 * @tparam offset The offset added to the raw signal (gotten using CANTemplateConvertFloat(float value))
 * @tparam signed_raw Whether or not the signal is signed
 * @tparam byte_order The order of bytes in the signal (big endian or little endian). Do not change this from the
 * default (little endian) if you aren't sure you need to.
 * @tparam mask This is calculated for you by default
 * @tparam unity_factor This is calculated for you by default
 */
template <typename SignalType,
          uint8_t input_position,
          uint8_t length,
          int factor,
          int offset,
          bool signed_raw = false,
          ICANSignal::ByteOrder byte_order = ICANSignal::ByteOrder::kLittleEndian,
          uint8_t position = CANSignal_generate_position(input_position, length, byte_order),
          uint64_t mask = CANSignal_generate_mask(position, length, byte_order),
          bool unity_factor = factor == CANTemplateConvertFloat(1)
                              && offset == 0>  // unity_factor is used for increased precision on unity-factor 64-bit
                                               // signals by getting rid of floating point error
class CANSignal : public ITypedCANSignal<SignalType>
{
    using underlying_type = typename GetCANRawType<signed_raw>::type;

public:
    CANSignal()
    {
        static_assert(factor != 0, "The integer representation of the factor for a CAN signal must not be 0");
        this->signal_ = static_cast<SignalType>(0);
    }

    void EncodeSignal(uint64_t *buffer) override { InternalEncodeSignal(buffer); }

    template <bool unity_factor_ = unity_factor, typename std::enable_if<unity_factor_, void>::type * = nullptr>
    void InternalEncodeSignal(uint64_t *buffer)
    {
        if (byte_order == ICANSignal::ByteOrder::kLittleEndian)
        {
            SignalType signal = this->signal_;
            *buffer |= (static_cast<underlying_type>(signal) << position) & mask;
        }
        else
        {
            SignalType signal = this->signal_;
            uint8_t temp_reversed_buffer[8]{0};
            void *temp_reversed_buffer_ptr{
                temp_reversed_buffer};  // intermediate as void* to get rid of strict aliasing compiler warnings
            *reinterpret_cast<underlying_type *>(temp_reversed_buffer_ptr) |=
                (static_cast<underlying_type>(signal) << (64 - (position + length)));
            std::reverse(std::begin(temp_reversed_buffer), std::end(temp_reversed_buffer));
            *buffer |= *reinterpret_cast<underlying_type *>(temp_reversed_buffer_ptr) & mask;
        }
    }

    template <bool unity_factor_ = unity_factor, typename std::enable_if<!unity_factor_, void>::type * = nullptr>
    void InternalEncodeSignal(uint64_t *buffer)
    {
        if (byte_order == ICANSignal::ByteOrder::kLittleEndian)
        {
            SignalType signal = this->signal_;
            *buffer |=
                (static_cast<underlying_type>(((signal - CANTemplateGetFloat(offset)) / CANTemplateGetFloat(factor)))
                 << position)
                & mask;
        }
        else
        {
            SignalType signal = this->signal_;
            uint8_t temp_reversed_buffer[8]{0};
            void *temp_reversed_buffer_ptr{temp_reversed_buffer};
            *reinterpret_cast<underlying_type *>(temp_reversed_buffer_ptr) |=
                (static_cast<underlying_type>(((signal - CANTemplateGetFloat(offset)) / CANTemplateGetFloat(factor)))
                 << (64 - (position + length)));
            std::reverse(std::begin(temp_reversed_buffer), std::end(temp_reversed_buffer));
            *buffer |= *reinterpret_cast<underlying_type *>(temp_reversed_buffer_ptr) & mask;
        }
    }

    void DecodeSignal(uint64_t *buffer) override { InternalDecodeSignal(buffer); }

    template <bool unity_factor_ = unity_factor, typename std::enable_if<unity_factor_, void>::type * = nullptr>
    void InternalDecodeSignal(uint64_t *buffer)
    {
        if (byte_order == ICANSignal::ByteOrder::kLittleEndian)
        {
            uint8_t temp_buffer[8]{0};
            void *temp_buffer_ptr{temp_buffer};
            *reinterpret_cast<underlying_type *>(temp_buffer_ptr) = *buffer & mask;
            this->signal_ = static_cast<SignalType>(
                (*reinterpret_cast<underlying_type *>(temp_buffer_ptr)) << (64 - (position + length)) >> (64 - length));
        }
        else
        {
            uint8_t temp_buffer[8]{0};
            void *temp_buffer_ptr{temp_buffer};
            *reinterpret_cast<underlying_type *>(temp_buffer_ptr) = *buffer & mask;
            std::reverse(std::begin(temp_buffer), std::end(temp_buffer));
            this->signal_ = static_cast<SignalType>((*reinterpret_cast<underlying_type *>(temp_buffer_ptr)) << position
                                                    >> (64 - length));
        }
    }

    template <bool unity_factor_ = unity_factor, typename std::enable_if<!unity_factor_, void>::type * = nullptr>
    void InternalDecodeSignal(uint64_t *buffer)
    {
        if (byte_order == ICANSignal::ByteOrder::kLittleEndian)
        {
            uint8_t temp_buffer[8]{0};
            void *temp_buffer_ptr{temp_buffer};
            *reinterpret_cast<underlying_type *>(temp_buffer_ptr) = *buffer & mask;
            this->signal_ =
                static_cast<SignalType>((static_cast<float>((*reinterpret_cast<underlying_type *>(temp_buffer_ptr))
                                                                << (64 - (position + length))
                                                            >> (64 - length))
                                         * CANTemplateGetFloat(factor))
                                        + CANTemplateGetFloat(offset));
        }
        else
        {
            uint8_t temp_buffer[8]{0};
            void *temp_buffer_ptr{temp_buffer};
            *reinterpret_cast<underlying_type *>(temp_buffer_ptr) = *buffer & mask;
            std::reverse(std::begin(temp_buffer), std::end(temp_buffer));
            this->signal_ = static_cast<SignalType>(
                (static_cast<float>((*reinterpret_cast<underlying_type *>(temp_buffer_ptr)) << position
                                    >> (64 - length))
                 * CANTemplateGetFloat(factor))
                + CANTemplateGetFloat(offset));
        }
    }

    void operator=(const SignalType &signal) { ITypedCANSignal<SignalType>::operator=(signal); }

    SignalType operator+=(const SignalType &signal) { return ITypedCANSignal<SignalType>::operator+=(signal); }

    SignalType operator-=(const SignalType &signal) { return ITypedCANSignal<SignalType>::operator-=(signal); }

    SignalType operator*=(const SignalType &signal) { return ITypedCANSignal<SignalType>::operator*=(signal); }

    SignalType operator/=(const SignalType &signal) { return ITypedCANSignal<SignalType>::operator/=(signal); }
};

// Macros for making signed and unsigned CAN signals, default little-endian
#define MakeEndianUnsignedCANSignal(SignalType, position, length, factor, offset, byte_order) \
    CANSignal<SignalType,                                                                     \
              position,                                                                       \
              length,                                                                         \
              CANTemplateConvertFloat(factor),                                                \
              CANTemplateConvertFloat(offset),                                                \
              false,                                                                          \
              byte_order>
#define MakeEndianSignedCANSignal(SignalType, position, length, factor, offset, byte_order) \
    CANSignal<SignalType,                                                                   \
              position,                                                                     \
              length,                                                                       \
              CANTemplateConvertFloat(factor),                                              \
              CANTemplateConvertFloat(offset),                                              \
              true,                                                                         \
              byte_order>
#define MakeUnsignedCANSignal(SignalType, position, length, factor, offset) \
    MakeEndianUnsignedCANSignal(SignalType, position, length, factor, offset, ICANSignal::ByteOrder::kLittleEndian)
#define MakeSignedCANSignal(SignalType, position, length, factor, offset) \
    MakeEndianSignedCANSignal(SignalType, position, length, factor, offset, ICANSignal::ByteOrder::kLittleEndian)

class ICANTXMessage
{
public:
    virtual uint32_t GetID() = 0;
#if !defined(NATIVE)  // workaround for unit tests
    virtual VirtualTimer &GetTransmitTimer() = 0;
#endif
    virtual void EncodeAndSend() = 0;
};

class ICANRXMessage
{
public:
    virtual uint32_t GetID() = 0;
    virtual void DecodeSignals(CANMessage message) = 0;  // Decodes signals if ID matches
};

class ICAN
{
public:
    enum class BaudRate
    {
        kBaud1M = 1000000,
        kBaud500K = 500000,
        kBaud250K = 250000,
        kBaud125k = 125000
    };

    virtual void Initialize(BaudRate baud) = 0;

    virtual bool SendMessage(CANMessage &msg) = 0;

    virtual void RegisterRXMessage(ICANRXMessage &msg) = 0;

    virtual void Tick() = 0;
};

/**
 * @brief This class is used to test CAN interface implementations without accessing hardware
 *
 */
class MockCAN : public ICAN
{
public:
    void Initialize(BaudRate baud __attribute__((unused))) {}
    bool SendMessage(CANMessage &msg)
    {
        /* for (size_t i = 0; i < rx_messages_.size(); i++)
        {
            if (rx_messages_[i]->GetID() == msg.id_)
            {
                rx_messages_[i]->DecodeSignals(msg);
            }
        } */
        last_message = msg;
        return true;
    }
    void RegisterRXMessage(ICANRXMessage &msg __attribute__((unused)))
    { /* rx_messages_.push_back(&msg); */
    }
    void Tick() {}

    CANMessage last_message{0, 8, std::array<uint8_t, 8>{0}};

private:
    // std::vector<ICANRXMessage *> rx_messages_; //not working in native unit tests
};

class IMultiplexedSignalGroup
{
public:
    virtual ICANSignal *at(size_t index) = 0;
    virtual size_t size() const = 0;

    uint64_t multiplexor_value_{0};
    bool always_active_{false};
};

template <size_t num_signals>
class MultiplexedSignalGroup : public std::array<ICANSignal *, num_signals>, public IMultiplexedSignalGroup
{
public:
    template <typename... Ts>
    MultiplexedSignalGroup(uint64_t multiplexor_value, Ts &... signals)
        : std::array<ICANSignal *, num_signals>{&signals...}
    {
        static_assert(sizeof...(signals) == num_signals, "Wrong number of signals passed into SignalGroup.");
        multiplexor_value_ = multiplexor_value;
        always_active_ = false;
    }

    template <typename... Ts>
    MultiplexedSignalGroup(bool always_active, uint64_t multiplexor_value, Ts &... signals)
        : std::array<ICANSignal *, num_signals>{&signals...}
    {
        static_assert(sizeof...(signals) == num_signals, "Wrong number of signals passed into SignalGroup.");
        multiplexor_value_ = multiplexor_value;
        always_active_ = always_active;
    }

    ICANSignal *at(size_t index) override { return std::array<ICANSignal *, num_signals>::at(index); }

    size_t size() const override { return std::array<ICANSignal *, num_signals>::size(); }
};

/**
 * @brief A class for storing signals in a message that sends every period
 */
template <size_t num_signals>
class CANTXMessage : public ICANTXMessage
{
public:
    template <typename... Ts>
    /**
     * @brief Construct a new CANTXMessage object
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message
     * @param extended_id Whether the ID is extended (true) or standard (false)
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param start_time The time in ms to start transmitting the message
     * @param signals The ICANSignals contained in the message
     */
    CANTXMessage(ICAN &can_interface,
                 uint32_t id,
                 bool extended_id,
                 uint8_t length,
                 uint32_t period,
                 ICANSignal &signal_1,
                 Ts &... signals)
        : can_interface_{can_interface},
          message_{id, extended_id, length, std::array<uint8_t, 8>()},
          transmit_timer_{period, [this]() { this->EncodeAndSend(); }, VirtualTimer::Type::kRepeating},
          signals_{&signal_1, &signals...}
    {
        static_assert(sizeof...(signals) == num_signals - 1, "Wrong number of signals passed into CANTXMessage.");
    }

    template <typename... Ts>
    /**
     * @brief Construct a new CANTXMessage object, default to standard id
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param start_time The time in ms to start transmitting the message
     * @param signals The ICANSignals contained in the message
     */
    CANTXMessage(
        ICAN &can_interface, uint32_t id, uint8_t length, uint32_t period, ICANSignal &signal_1, Ts &... signals)
        : CANTXMessage(can_interface, id, false, length, period, signal_1, signals...)
    {
    }

    template <typename... Ts>
    /**
     * @brief Construct a new CANTXMessage object and automatically adds it to a VirtualTimerGroup
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message
     * @param extended_id Whether the ID is extended (true) or standard (false)
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param start_time The time in ms to start transmitting the message
     * @param timer_group A timer group to add the transmit timer to
     * @param signals The ICANSignals contained in the message
     */
    CANTXMessage(ICAN &can_interface,
                 uint32_t id,
                 bool extended_id,
                 uint8_t length,
                 uint32_t period,
                 VirtualTimerGroup &timer_group,
                 ICANSignal &signal_1,
                 Ts &... signals)
        : CANTXMessage(can_interface, id, extended_id, length, period, signal_1, signals...)
    {
        timer_group.AddTimer(transmit_timer_);
    }

    template <typename... Ts>
    /**
     * @brief Construct a new CANTXMessage object and automatically adds it to a VirtualTimerGroup, default to standard
     * id
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param start_time The time in ms to start transmitting the message
     * @param timer_group A timer group to add the transmit timer to
     * @param signals The ICANSignals contained in the message
     */
    CANTXMessage(ICAN &can_interface,
                 uint32_t id,
                 uint8_t length,
                 uint32_t period,
                 VirtualTimerGroup &timer_group,
                 ICANSignal &signal_1,
                 Ts &... signals)
        : CANTXMessage(can_interface, id, false, length, period, timer_group, signal_1, signals...)
    {
    }

    void EncodeAndSend() override
    {
        EncodeSignals();
        can_interface_.SendMessage(message_);
    }

    uint32_t GetID() override { return message_.id_; }

#if !defined(NATIVE)  // workaround for unit tests
    VirtualTimer &GetTransmitTimer() override { return transmit_timer_; }
#endif

    void Enable() { transmit_timer_.Enable(); }
    void Disable() { transmit_timer_.Disable(); }

private:
    ICAN &can_interface_;
    CANMessage message_;
    VirtualTimer transmit_timer_;
    std::array<ICANSignal *, num_signals> signals_;

    void EncodeSignals()
    {
        uint8_t temp_raw[8]{0};
        for (uint8_t i = 0; i < num_signals; i++)
        {
            signals_.at(i)->EncodeSignal(reinterpret_cast<uint64_t *>(temp_raw));
        }
        std::copy(std::begin(temp_raw), std::end(temp_raw), message_.data_.begin());
    }
};

template <size_t num_groups, size_t num_multiplexors_to_transmit, typename MultiplexorType>
class MultiplexedCANTXMessage : public ICANTXMessage
{
public:
    template <typename... Ts>
    /**
     * @brief Construct a new MultiplexedCANTXMessage object, for use if your message uses a multiplexor
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message
     * @param extended_id Whether the ID is extended (true) or standard (false)
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param multiplexor_values_to_transmit An array of multiplexor values that gets cycled through to automatically
     * transmit the messages
     * @param multiplexor The CANSignal that will be used to multiplex the message
     * @param signal_groups The SignalGroups contained in the message
     */
    MultiplexedCANTXMessage(ICAN &can_interface,
                            uint32_t id,
                            bool extended_id,
                            uint8_t length,
                            uint32_t period,
                            std::array<MultiplexorType, num_multiplexors_to_transmit> multiplexor_values_to_transmit,
                            ITypedCANSignal<MultiplexorType> &multiplexor,
                            Ts &... signal_groups)
        : can_interface_{can_interface},
          message_{id, extended_id, length, std::array<uint8_t, 8>()},
#if !defined(NATIVE)  // workaround for unit tests
          transmit_timer_{period, [this]() { this->EncodeAndSend(); }, VirtualTimer::Type::kRepeating},
#endif
          multiplexor_values_to_transmit_{multiplexor_values_to_transmit},
          multiplexor_{&multiplexor},
          signal_groups_{&signal_groups...}
    {
        static_assert(sizeof...(signal_groups) == num_groups,
                      "Wrong number of signal groups passed into MultiplexedCANTXMessage.");
        for (size_t i = 0; i < signal_groups_.size(); i++)
        {
            if (signal_groups_.at(i)->always_active_)
            {
                has_always_active_signal_group_ = true;
                always_active_signal_group_index_ = i;
                break;
            }
        }
    }

    template <typename... Ts>
    /**
     * @brief Construct a new MultiplexedCANTXMessage object, for use if your message uses a multiplexor, default to
     * standard id
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param multiplexor_values_to_transmit An array of multiplexor values that gets cycled through to automatically
     * transmit the messages
     * @param multiplexor The CANSignal that will be used to multiplex the message
     * @param signal_groups The SignalGroups contained in the message
     */
    MultiplexedCANTXMessage(ICAN &can_interface,
                            uint32_t id,
                            uint8_t length,
                            uint32_t period,
                            std::array<MultiplexorType, num_multiplexors_to_transmit> multiplexor_values_to_transmit,
                            ITypedCANSignal<MultiplexorType> &multiplexor,
                            Ts &... signal_groups)
        : MultiplexedCANTXMessage(
            can_interface, id, false, length, period, multiplexor_values_to_transmit, multiplexor, signal_groups...)
    {
    }

    template <typename... Ts>
    /**
     * @brief Construct a new MultiplexedCANTXMessage object, for use if your message uses a multiplexor, and
     * automatically adds it to a VirtualTimerGroup
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message
     * @param extended_id Whether the ID is extended (true) or standard (false)
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param timer_group A timer group to add the transmit timer to
     * @param multiplexor_values_to_transmit An array of multiplexor values that gets cycled through to automatically
     * transmit the messages
     * @param multiplexor The CANSignal that will be used to multiplex the message
     * @param signal_groups The SignalGroups contained in the message
     */
    MultiplexedCANTXMessage(ICAN &can_interface,
                            uint32_t id,
                            bool extended_id,
                            uint8_t length,
                            uint32_t period,
                            VirtualTimerGroup &timer_group,
                            std::array<MultiplexorType, num_multiplexors_to_transmit> multiplexor_values_to_transmit,
                            ITypedCANSignal<MultiplexorType> &multiplexor,
                            Ts &... signal_groups)
        : MultiplexedCANTXMessage(can_interface,
                                  id,
                                  extended_id,
                                  length,
                                  period,
                                  multiplexor_values_to_transmit,
                                  multiplexor,
                                  signal_groups...)
    {
#if !defined(NATIVE)  // workaround for unit tests
        timer_group.AddTimer(transmit_timer_);
#endif
    }

    template <typename... Ts>
    /**
     * @brief Construct a new MultiplexedCANTXMessage object, for use if your message uses a multiplexor, and
     * automatically adds it to a VirtualTimerGroup, default to standard id
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param timer_group A timer group to add the transmit timer to
     * @param multiplexor_values_to_transmit An array of multiplexor values that gets cycled through to automatically
     * transmit the messages
     * @param multiplexor The CANSignal that will be used to multiplex the message
     * @param signal_groups The SignalGroups contained in the message
     */
    MultiplexedCANTXMessage(ICAN &can_interface,
                            uint32_t id,
                            uint8_t length,
                            uint32_t period,
                            VirtualTimerGroup &timer_group,
                            std::array<MultiplexorType, num_multiplexors_to_transmit> multiplexor_values_to_transmit,
                            ITypedCANSignal<MultiplexorType> &multiplexor,
                            Ts &... signal_groups)
        : MultiplexedCANTXMessage(can_interface,
                                  id,
                                  false,
                                  length,
                                  period,
                                  timer_group,
                                  multiplexor_values_to_transmit,
                                  multiplexor,
                                  signal_groups...)
    {
    }

    void EncodeAndSend() override  // increments multiplexor automatically
    {
        *multiplexor_ = multiplexor_values_to_transmit_.at(static_cast<size_t>(multiplexor_index_));
        EncodeSignals(GetSignalGroupIndex(*multiplexor_));
        can_interface_.SendMessage(message_);
        if (multiplexor_index_ < num_multiplexors_to_transmit - 1)
        {
            multiplexor_index_ += 1;
        }
        else
        {
            multiplexor_index_ = 0;
        }
    }

    void EncodeAndSend(MultiplexorType multiplexor_value)
    {
        *multiplexor_ = multiplexor_value;
        EncodeSignals(GetSignalGroupIndex(*multiplexor_));
        can_interface_.SendMessage(message_);
    }

    uint32_t GetID() override { return message_.id_; }

#if !defined(NATIVE)  // workaround for unit tests
    VirtualTimer &GetTransmitTimer() override { return transmit_timer_; }
#endif

    void Enable() { transmit_timer_.Enable(); }
    void Disable() { transmit_timer_.Disable(); }

private:
    ICAN &can_interface_;
    CANMessage message_;
    VirtualTimer transmit_timer_;
    std::array<MultiplexorType, num_multiplexors_to_transmit> multiplexor_values_to_transmit_;
    ITypedCANSignal<MultiplexorType> *multiplexor_;
    std::array<IMultiplexedSignalGroup *, num_groups> signal_groups_;
    bool has_always_active_signal_group_{false};
    uint64_t always_active_signal_group_index_{0};

    uint64_t multiplexor_index_ = 0;

    uint64_t GetSignalGroupIndex(MultiplexorType multiplexor_value)
    {
        size_t index = 0xFFFFFFFFul;  // init to invalid value
        for (size_t i = 0; i < num_groups; i++)
        {
            if (static_cast<uint64_t>(multiplexor_value) == signal_groups_.at(i)->multiplexor_value_)
            {
                index = i;
                break;
            }
        }
        return index;
    }

    void EncodeSignals(uint64_t signal_group_index)
    {
        uint8_t temp_raw[8]{0};
        multiplexor_->EncodeSignal(reinterpret_cast<uint64_t *>(temp_raw));
        if (has_always_active_signal_group_)
        {
            for (uint8_t i = 0; i < signal_groups_.at(static_cast<size_t>(always_active_signal_group_index_))->size();
                 i++)
            {
                signal_groups_.at(static_cast<size_t>(always_active_signal_group_index_))
                    ->at(i)
                    ->EncodeSignal(reinterpret_cast<uint64_t *>(temp_raw));
            }
        }
        if (signal_group_index != 0xFFFFFFFFul)  // not invalid value
        {
            for (uint8_t i = 0; i < signal_groups_.at(static_cast<size_t>(signal_group_index))->size(); i++)
            {
                signal_groups_.at(static_cast<size_t>(signal_group_index))
                    ->at(i)
                    ->EncodeSignal(reinterpret_cast<uint64_t *>(temp_raw));
            }
        }

        std::copy(std::begin(temp_raw), std::end(temp_raw), message_.data_.begin());
    }
};

template <size_t num_signals>
class PGNCANTXMessage : public ICANTXMessage
{
public:
    template <typename... Ts>
    /**
     * @brief Construct a new PGNCANTXMessage object for use with J1939 or NMEA2000
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message as a PGN object
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param signals The ICANSignals contained in the message
     */
    PGNCANTXMessage(ICAN &can_interface,
                    PGNCANMessage::ExtendedId id,
                    uint8_t length,
                    uint32_t period,
                    ICANSignal &signal_1,
                    Ts &... signals)
        : can_interface_{can_interface},
          message_{id, length, std::array<uint8_t, 8>()},
          transmit_timer_{period, [this]() { this->EncodeAndSend(); }, VirtualTimer::Type::kRepeating},
          signals_{&signal_1, &signals...}
    {
        static_assert(sizeof...(signals) == num_signals - 1, "Wrong number of signals passed into PGNCANTXMessage.");
    }

    template <typename... Ts>
    /**
     * @brief Construct a new PGNCANTXMessage object for use with J1939 or NMEA2000 and automatically adds it to a
     * VirtualTimerGroup
     *
     * @param can_interface The ICAN object the message will be transmitted on
     * @param id The ID of the CAN message as a PGN object
     * @param extended_id Whether the ID is extended (true) or standard (false)
     * @param length The length in bytes of the message
     * @param period The transmit period in ms of the message
     * @param timer_group A timer group to add the transmit timer to
     * @param signals The ICANSignals contained in the message
     */
    PGNCANTXMessage(ICAN &can_interface,
                    PGNCANMessage::ExtendedId id,
                    uint8_t length,
                    uint32_t period,
                    VirtualTimerGroup &timer_group,
                    ICANSignal &signal_1,
                    Ts &... signals)
        : PGNCANTXMessage(can_interface, id, length, period, signal_1, signals...)
    {
        timer_group.AddTimer(transmit_timer_);
    }

    void EncodeAndSend() override
    {
        EncodeSignals();
        can_interface_.SendMessage(message_);
    }

    uint32_t GetID() override { return message_.id_; }

#if !defined(NATIVE)  // workaround for unit tests
    VirtualTimer &GetTransmitTimer() override { return transmit_timer_; }
#endif

    void Enable() { transmit_timer_.Enable(); }
    void Disable() { transmit_timer_.Disable(); }

private:
    ICAN &can_interface_;
    PGNCANMessage message_;
    VirtualTimer transmit_timer_;
    std::array<ICANSignal *, num_signals> signals_;

    void EncodeSignals()
    {
        uint8_t temp_raw[8]{0};
        for (uint8_t i = 0; i < num_signals; i++)
        {
            signals_.at(i)->EncodeSignal(reinterpret_cast<uint64_t *>(temp_raw));
        }
        std::copy(std::begin(temp_raw), std::end(temp_raw), message_.data_.begin());
    }
};

/**
 * @brief A class for storing signals that get updated every time a matching message is received
 */
template <size_t num_signals>
class CANRXMessage : public ICANRXMessage
{
public:
    template <typename... Ts>
    CANRXMessage(ICAN &can_interface,
                 uint32_t id,
                 std::function<uint32_t(void)> get_millis,
                 std::function<void(void)> callback_function,
                 ICANSignal &signal_1,
                 Ts &... signals)
        : can_interface_{can_interface},
          id_{id},
          get_millis_{get_millis},
          callback_function_{callback_function},
          signals_{&signal_1, &signals...}
    {
        static_assert(sizeof...(signals) == num_signals - 1, "Wrong number of signals passed into CANRXMessage.");
        can_interface_.RegisterRXMessage(*this);
    }

    template <typename... Ts>
    CANRXMessage(ICAN &can_interface,
                 uint32_t id,
                 std::function<uint32_t(void)> get_millis,
                 ICANSignal &signal_1,
                 Ts &... signals)
        : CANRXMessage{can_interface, id, get_millis, nullptr, signal_1, signals...}
    {
    }

// If compiling for Arduino, automatically uses millis() instead of requiring a std::function<uint32_t(void)> to get the
// current time
#ifdef ARDUINO
    template <typename... Ts>
    CANRXMessage(ICAN &can_interface,
                 uint32_t id,
                 std::function<void(void)> callback_function,
                 ICANSignal &signal_1,
                 Ts &... signals)
        : CANRXMessage{can_interface, id, []() { return millis(); }, callback_function, signal_1, signals...}
    {
    }

    template <typename... Ts>
    CANRXMessage(ICAN &can_interface, uint32_t id, ICANSignal &signal_1, Ts &... signals)
        : CANRXMessage{can_interface, id, []() { return millis(); }, nullptr, signal_1, signals...}
    {
    }
#endif

    uint32_t GetID() { return id_; }

    void DecodeSignals(CANMessage message)
    {
        if (message.id_ != id_)
        {
            return;
        }
        uint64_t temp_raw = *reinterpret_cast<uint64_t *>(message.data_.data());
        for (uint8_t i = 0; i < num_signals; i++)
        {
            signals_[i]->DecodeSignal(&temp_raw);
        }

        // DecodeSignals is called only on message received
        if (callback_function_)
        {
            callback_function_();
        }

        last_receive_time_ = get_millis_();
    }

    uint32_t GetLastReceiveTime() const { return last_receive_time_; }
    uint32_t GetTimeSinceLastReceive() const { return get_millis_() - last_receive_time_; }

private:
    ICAN &can_interface_;
    uint32_t id_;
    // A function to get the current time in millis on the current platform
    std::function<uint32_t(void)> get_millis_;

    // The callback function should be a very short function that will get called every time a new message is received.
    std::function<void(void)> callback_function_;

    std::array<ICANSignal *, num_signals> signals_;

    uint64_t raw_message;

    uint32_t last_receive_time_ = 0;
};

template <size_t num_groups, typename MultiplexorType>
class MultiplexedCANRXMessage : public ICANRXMessage
{
public:
    /**
     * @brief Construct a new MultiplexedCANRXMessage object, for use if your message uses a multiplexor
     *
     * @param can_interface The ICAN object the message will be received from
     * @param id The ID of the message
     * @param get_millis A function to get the current time in milliseconds
     * @param callback_function A function to be called on message reception
     * @param multiplexor The signal to be used as a multiplexor
     * @param signal_groups The signalgroups to be decoded, selected based on the value of the multiplexor
     */
    template <typename... Ts>
    MultiplexedCANRXMessage(ICAN &can_interface,
                            uint32_t id,
                            std::function<uint32_t(void)> get_millis,
                            std::function<void(void)> callback_function,
                            ITypedCANSignal<MultiplexorType> &multiplexor,
                            Ts &... signal_groups)
        : can_interface_{can_interface},
          id_{id},
          get_millis_{get_millis},
          callback_function_{callback_function},
          multiplexor_{&multiplexor},
          signal_groups_{&signal_groups...}
    {
        static_assert(sizeof...(signal_groups) == num_groups,
                      "Wrong number of SignalGroups passed into MultiplexedCANRXMessage.");
        can_interface_.RegisterRXMessage(*this);
        for (size_t i = 0; i < signal_groups_.size(); i++)
        {
            if (signal_groups_.at(i)->always_active_)
            {
                has_always_active_signal_group_ = true;
                always_active_signal_group_index_ = i;
                break;
            }
        }
    }

    template <typename... Ts>
    MultiplexedCANRXMessage(ICAN &can_interface,
                            uint32_t id,
                            std::function<uint32_t(void)> get_millis,
                            ITypedCANSignal<MultiplexorType> &multiplexor,
                            Ts &... signal_groups)
        : MultiplexedCANRXMessage{can_interface, id, get_millis, nullptr, multiplexor, signal_groups...}
    {
    }

// If compiling for Arduino, automatically uses millis() instead of requiring a std::function<uint32_t(void)> to get the
// current time
#ifdef ARDUINO
    template <typename... Ts>
    MultiplexedCANRXMessage(ICAN &can_interface,
                            uint32_t id,
                            std::function<void(void)> callback_function,
                            ITypedCANSignal<MultiplexorType> &multiplexor,
                            Ts &... signal_groups)
        : MultiplexedCANRXMessage{
            can_interface, id, []() { return millis(); }, callback_function, multiplexor, signal_groups...}
    {
    }

    template <typename... Ts>
    MultiplexedCANRXMessage(ICAN &can_interface,
                            uint32_t id,
                            ITypedCANSignal<MultiplexorType> &multiplexor,
                            Ts &... signal_groups)
        : MultiplexedCANRXMessage{can_interface, id, []() { return millis(); }, nullptr, multiplexor, signal_groups...}
    {
    }
#endif

    uint32_t GetID() { return id_; }

    void DecodeSignals(CANMessage message)
    {
        if (message.id_ != id_)
        {
            return;
        }

        uint64_t temp_raw = *reinterpret_cast<uint64_t *>(message.data_.data());

        if (has_always_active_signal_group_)
        {
            for (uint8_t i = 0; i < signal_groups_.at(static_cast<size_t>(always_active_signal_group_index_))->size();
                 i++)
            {
                signal_groups_.at(static_cast<size_t>(always_active_signal_group_index_))
                    ->at(i)
                    ->DecodeSignal(&temp_raw);
            }
        }

        multiplexor_->DecodeSignal(&temp_raw);
        size_t multiplexor_index = 0xFFFFFFFFul;  // init to invalid value
        for (size_t i = 0; i < num_groups; i++)
        {
            MultiplexorType multiplexor_value = *multiplexor_;
            if (static_cast<uint64_t>(multiplexor_value) == signal_groups_.at(i)->multiplexor_value_)
            {
                multiplexor_index = i;
                break;
            }
        }

        if (multiplexor_index == 0xFFFFFFFFull)  // If the multiplexor is invalid, don't decode any signals
        {
            return;
        }
        for (uint8_t i = 0; i < signal_groups_.at(multiplexor_index)->size(); i++)
        {
            signal_groups_.at(multiplexor_index)->at(i)->DecodeSignal(&temp_raw);
        }

        // DecodeSignals is called only on message received
        if (callback_function_)
        {
            callback_function_();
        }

        last_receive_time_ = get_millis_();
    }

    uint32_t GetLastReceiveTime() const { return last_receive_time_; }
    uint32_t GetTimeSinceLastReceive() const { return get_millis_() - last_receive_time_; }

private:
    ICAN &can_interface_;
    uint32_t id_;
    // A function to get the current time in millis on the current platform
    std::function<uint32_t(void)> get_millis_;

    // The callback function should be a very short function that will get called every time a new message is received.
    std::function<void(void)> callback_function_;

    ITypedCANSignal<MultiplexorType> *multiplexor_;
    std::array<IMultiplexedSignalGroup *, num_groups> signal_groups_;
    bool has_always_active_signal_group_{false};
    uint64_t always_active_signal_group_index_{0};

    uint64_t multiplexor_index{0};

    uint64_t raw_message;

    uint32_t last_receive_time_ = 0;
};

template <size_t num_signals>
class PGNCANRXMessage : public ICANRXMessage
{
public:
    /**
     * @brief Construct a new PGNCANRXMessage object, for use receiving J1939 or NMEA2000 messages
     *
     * @param can_interface The ICAN object the message will be received on
     * @param id The ID of the message, as a PGN object
     * @param get_millis A function to get the current time in milliseconds
     * @param callback_function An optional callback function to get called on message reception
     * @param signals The signals contained in the message
     */
    template <typename... Ts>
    PGNCANRXMessage(ICAN &can_interface,
                    PGNCANMessage::ExtendedId id,
                    std::function<uint32_t(void)> get_millis,
                    std::function<void(void)> callback_function,
                    ICANSignal &signal_1,
                    Ts &... signals)
        : can_interface_{can_interface},
          id_{id},
          get_millis_{get_millis},
          callback_function_{callback_function},
          signals_{&signal_1, &signals...}
    {
        static_assert(sizeof...(signals) == num_signals - 1, "Wrong number of signals passed into CANRXMessage.");
        can_interface_.RegisterRXMessage(*this);
    }

    template <typename... Ts>
    PGNCANRXMessage(ICAN &can_interface,
                    PGNCANMessage::ExtendedId id,
                    std::function<uint32_t(void)> get_millis,
                    ICANSignal &signal_1,
                    Ts &... signals)
        : PGNCANRXMessage{can_interface, id, get_millis, nullptr, signal_1, signals...}
    {
    }

// If compiling for Arduino, automatically uses millis() instead of requiring a std::function<uint32_t(void)> to get the
// current time
#ifdef ARDUINO
    template <typename... Ts>
    PGNCANRXMessage(ICAN &can_interface,
                    PGNCANMessage::ExtendedId id,
                    std::function<void(void)> callback_function,
                    ICANSignal &signal_1,
                    Ts &... signals)
        : PGNCANRXMessage{can_interface, id, []() { return millis(); }, callback_function, signal_1, signals...}
    {
    }

    template <typename... Ts>
    PGNCANRXMessage(ICAN &can_interface, PGNCANMessage::ExtendedId id, ICANSignal &signal_1, Ts &... signals)
        : PGNCANRXMessage{can_interface, id, []() { return millis(); }, nullptr, signal_1, signals...}
    {
    }
#endif

    uint32_t GetID() { return id_; }

    void DecodeSignals(CANMessage message)
    {
        PGNCANMessage::PGN incoming_pgn =
            static_cast<PGNCANMessage::PGN>(static_cast<PGNCANMessage::ExtendedId>(message.id_).extended_id.pgn);
        PGNCANMessage::PGN pgn = static_cast<PGNCANMessage::PGN>(id_.extended_id.pgn);
        if (incoming_pgn.raw != pgn.raw)
        {
            return;
        }
        uint64_t temp_raw = *reinterpret_cast<uint64_t *>(message.data_.data());
        for (uint8_t i = 0; i < num_signals; i++)
        {
            signals_[i]->DecodeSignal(&temp_raw);
        }

        // DecodeSignals is called only on message received
        if (callback_function_)
        {
            callback_function_();
        }

        last_receive_time_ = get_millis_();
    }

    uint32_t GetLastReceiveTime() const { return last_receive_time_; }
    uint32_t GetTimeSinceLastReceive() const { return get_millis_() - last_receive_time_; }

private:
    ICAN &can_interface_;
    PGNCANMessage::ExtendedId id_;
    // A function to get the current time in millis on the current platform
    std::function<uint32_t(void)> get_millis_;

    // The callback function should be a very short function that will get called every time a new message is received.
    std::function<void(void)> callback_function_;

    std::array<ICANSignal *, num_signals> signals_;

    uint64_t raw_message;

    uint32_t last_receive_time_ = 0;
};
