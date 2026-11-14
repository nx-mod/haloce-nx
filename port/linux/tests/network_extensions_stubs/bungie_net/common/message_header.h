/* bungie_net's message_header.h, as far as network_extensions.c uses it */
#ifndef __MESSAGE_HEADER_H
#define __MESSAGE_HEADER_H
#define GET_MESSAGE_TYPE(message) (((message) >> 2) & 3)
#define GET_MESSAGE_SIZE(message) ((message) >> 4)
void *create_message(long type, void const *data, unsigned long data_size, void *buffer, unsigned short buffer_size);
#endif
