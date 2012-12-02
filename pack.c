#include "pack.h"

int pack(void *buffer, const char *format, ...) {
	va_list args;
	char *buff = (char*) buffer;
	uint16_t _word;
	uint32_t _long;
	char *_str;
	
	


	va_start(args, format);
	for (; *format != '\0'; format++) {
		switch (*format) {
			case 'b':
				*buff = (uint8_t) va_arg(args, int);
				buff++;
				break;
			
			case 'w':
				_word = (uint16_t) va_arg(args, int);
				_word = htons(_word);
				memcpy(buff, &_word, 2);
				buff += 2;
				break;
			
			case 'l':
				_long = (uint32_t) va_arg(args, int);
				_long = htonl(_long);
				memcpy(buff, &_long, 4);
				buff += 4;
				break;
			
			case 's':
			case 'S':
				_str = va_arg(args, char*);
				for (; *_str != '\0'; _str++, buff++)
					*buff = *_str;
				if (*format == 'S') *(buff++) = '\0';
		}
	}
	va_end(args);
	
	return buff - (char *) buffer;
}
