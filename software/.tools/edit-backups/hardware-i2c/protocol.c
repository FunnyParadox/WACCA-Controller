#include "protocol.h"
#include <string.h>
static uint16_t crc_byte(uint16_t crc, uint8_t b)
{
    crc ^= (uint16_t)((uint16_t)(b) << 8);
    for (unsigned i = 0; i < 8; ++i) crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000U) * 0x1021U));
    return (crc);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint16_t)(p[0]) | ((uint16_t)(p[1]) << 8)); }
static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x); p[1] = (uint8_t)(x >> 8); }

void wacca_decoder_reset(struct wacca_decoder *d) { d->used = 0; d->discard = false; }
bool wacca_decoder_feed(struct wacca_decoder *d, uint8_t byte, struct wacca_message *m)
{
    if (byte != 0)
    {
        if (!d->discard)
        {
            if (d->used == sizeof(d->buffer))
                d->discard = true;
            else d->buffer[d->used++] = byte;
        }
        return (false);
    }
    if (d->discard)
    {
        wacca_decoder_reset(d);
        d->rejected++;
        return (false);
    }
    const size_t input = d->used;
    d->used = 0;
    if (!input) return (false);

    // Decode in place: write never overtakes read, one buffer saves WACCA_MAX_RAW bytes
    size_t read = 0, write = 0;
    while (read < input)
    {
        const unsigned code = d->buffer[read++];
        if (code == 0 || code-1U > input-read || code-1U > WACCA_MAX_RAW - write) goto reject;

        for (unsigned i = 1; i < code; ++i) d->buffer[write++] = d->buffer[read++];
        if (code != 255 && read < input)
        {
            if (write == WACCA_MAX_RAW) goto reject;
            d->buffer[write++] = 0;
        }
    }
    if (write < 8 || d->buffer[0] != WACCA_PROTOCOL_VERSION) goto reject;

    const uint16_t length = get16(d->buffer + 4);
    if (length > WACCA_MAX_PAYLOAD || write != (size_t)(length) + 8U) goto reject;

    uint16_t crc=0xFFFF;
    for (size_t i = 0; i < write - 2; ++i) crc = crc_byte(crc, d->buffer[i]);
    if (crc != get16(d->buffer + write - 2)) goto reject;

    m->type = d->buffer[1];
    m->sequence = get16(d->buffer+2);
    m->length = length;
    m->payload = d->buffer + 6;
    return (true);

    reject:
    {
        d->rejected++;
        return (false);
    }
}
size_t wacca_message_encode(uint8_t type, uint16_t sequence, const uint8_t *payload, size_t length, uint8_t *out, size_t capacity)
{
    if (length > WACCA_MAX_PAYLOAD || (!payload && length) || !out || capacity < (((length + 8U) + ((length + 8U) / 254U)) + 2U)) return (0);
    // Stream header/payload/CRC directly into COBS; no large stack buffer
    uint8_t header[6] = {WACCA_PROTOCOL_VERSION, type, 0, 0, 0, 0};
    put16(header+2,sequence); put16(header + 4,(uint16_t)length);

    uint16_t crc = 0xFFFF;
    for (unsigned i = 0; i < 6; ++i) crc = crc_byte(crc, header[i]);
    for (size_t i = 0; i < length; ++i) crc = crc_byte(crc, payload[i]);

    uint8_t code = 1;
    size_t code_index = 0, write = 1;
    for (size_t i=0; i < length + 8U; ++i)
    {
        const uint8_t b = (i < 6) ? header[i] : ((i < (length + 6U)) ? payload[i - 6] : (i == (length + 6U) ? (uint8_t)(crc) : (uint8_t)(crc >> 8)));
        if (!b)
        {
            out[code_index] = code;
            code_index = write++;
            code = 1;
        }
        else
        {
            out[write++] = b;
            if (++code == 0xFF)
            {
                out[code_index] = code;
                code_index = write++;
                code = 1;
            }
        }
    }
    out[code_index] = code;
    out[write++] = 0;
    
    return (write);
}


