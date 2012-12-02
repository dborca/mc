#ifndef ZUTIL_H_included
#define ZUTIL_H_included

#include "../slib/array.h"


/* buffered I/O **************************************************************/


typedef struct FBUF FBUF;


/**
 * Alocate file structure and associate file descriptor to it.
 *
 * \param fd file descriptor
 *
 * \return file structure
 */
FBUF *f_dopen (int fd);


/**
 * Free file structure without closing the file.
 *
 * \param fs file structure
 *
 * \return 0 on success, non-zero on error
 */
int f_free (FBUF *fs);


/**
 * Open a binary temporary file in R/W mode.
 *
 * \return file structure
 *
 * \note the file will be deleted when closed
 */
FBUF *f_temp (void);


/**
 * Open a binary file in specified mode.
 *
 * \param filename file name
 * \param flags open mode, a combination of O_RDONLY, O_WRONLY, O_RDWR
 *
 * \return file structure
 */
FBUF *f_open (const char *filename, int flags);


/**
 * Read bytes from file.
 *
 * \param fs file structure
 * \param buf destination buffer
 * \param size size of buffer
 *
 * \return number of bytes read
 *
 * \note buf will not be null-terminated
 */
size_t f_read (FBUF *fs, char *buf, size_t size);


/**
 * Read a line of bytes from file until newline or EOF.
 *
 * \param buf destination buffer
 * \param size size of buffer
 * \param fs file structure
 *
 * \return number of bytes read
 *
 * \note does not stop on null-byte
 * \note buf will not be null-terminated
 */
size_t f_gets (char *buf, size_t size, FBUF *fs);


/**
 * Read one character from file.
 *
 * \param fs file structure
 *
 * \return character
 */
int f_getc (FBUF *fs);


/**
 * Seek into file.
 *
 * \param fs file structure
 * \param off offset
 * \param whence seek directive: SEEK_SET, SEEK_CUR or SEEK_END
 *
 * \return position in file, starting from begginning
 *
 * \note avoids thrashing read cache when possible
 */
off_t f_seek (FBUF *fs, off_t off, int whence);


/**
 * Seek to the beginning of file, thrashing read cache.
 *
 * \param fs file structure
 *
 * \return 0 if success, non-zero on error
 */
off_t f_reset (FBUF *fs);


/**
 * Write bytes to file.
 *
 * \param fs file structure
 * \param buf source buffer
 * \param size size of buffer
 *
 * \return number of written bytes, -1 on error
 *
 * \note thrashes read cache
 */
ssize_t f_write (FBUF *fs, const char *buf, size_t size);


/**
 * Truncate file to the current position.
 *
 * \param fs file structure
 *
 * \return current file size on success, negative on error
 *
 * \note thrashes read cache
 */
off_t f_trunc (FBUF *fs);


/**
 * Close file.
 *
 * \param fs file structure
 *
 * \return 0 on success, non-zero on error
 *
 * \note if this is temporary file, it is deleted
 */
int f_close (FBUF *fs);


/**
 * Create pipe stream to process.
 *
 * \param cmd shell command line
 * \param flags open mode, either O_RDONLY or O_WRONLY
 *
 * \return file structure
 */
FBUF *p_open (const char *cmd, int flags);


/**
 * Close pipe stream.
 *
 * \param fs structure
 *
 * \return 0 on success, non-zero on error
 */
int p_close (FBUF *fs);


/* search ********************************************************************/


/**
 * Locate a sub-buffer.
 *
 * \param haystack buffer to search into
 * \param i start position to search into haystack
 * \param hlen haystack length
 * \param needle buffer to search for
 * \param nlen needle length
 * \param whole search whole word only
 *
 * \return the first occurrence of the sub-buffer needle in the buffer haystack
 *
 * \note unlike strstr, this function doesn't care about '\0'
 */
const unsigned char *memmem_dumb (const unsigned char *haystack, size_t i, size_t hlen, const unsigned char *needle, size_t nlen, int whole);


/**
 * Locate a sub-buffer (case insensitive).
 *
 * \param haystack buffer to search into
 * \param i start position to search into haystack
 * \param hlen haystack length
 * \param needle buffer to search for
 * \param nlen needle length
 * \param whole search whole word only
 *
 * \return the first occurrence of the sub-buffer needle in the buffer haystack
 *
 * \note unlike strstr, this function doesn't care about '\0'
 */
const unsigned char *memmem_dumb_nocase (const unsigned char *haystack, size_t i, size_t hlen, const unsigned char *needle, size_t nlen, int whole);


/* stuff *********************************************************************/


/**
 * Read decimal number from string.
 *
 * \param[in,out] str string to parse
 * \param[out] n extracted number
 *
 * \return 0 if success, otherwise non-zero
 */
int scan_deci (const char **str, int *n);


/**
 * Count the number of digits in unsigned int.
 *
 * \param n number to check
 *
 * \return number of digits
 */
int get_digits (unsigned int n);

#endif
