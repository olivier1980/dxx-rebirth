#ifndef BUILD_DXXSCONF_H_SEEN
#define BUILD_DXXSCONF_H_SEEN

// '/usr/bin/g++-12' => 'g++-12 (Ubuntu 12.3.0-17ubuntu1) 12.3.0'
// '/usr/bin/g++-12 -g -O2 -ftabstop=4 -Wall -Wformat=2 -Wextra -Wmissing-braces -Wmissing-include-dirs -Wuninitialized -Wundef -Wpointer-arith -Wcast-qual -Wmissing-declarations -Wvla -pthread -funsigned-char -print-prog-name=as' => b'as'
// b'as' => 'GNU assembler (GNU Binutils for Ubuntu) 2.42'
// '/usr/bin/g++-12 -g -O2 -ftabstop=4 -Wall -Wformat=2 -Wextra -Wmissing-braces -Wmissing-include-dirs -Wuninitialized -Wundef -Wpointer-arith -Wcast-qual -Wmissing-declarations -Wvla -pthread -funsigned-char -print-prog-name=ld' => b'ld'
// b'ld' => 'GNU ld (GNU Binutils for Ubuntu) 2.42'

#define DXX_BUILD_DESCENT 1

#define DXX_WORDS_BIGENDIAN 0
#define DXX_WORDS_NEED_ALIGNMENT 0
#define DXX_USE_OGL 1
#define DXX_USE_OGLES 0
#define DXX_USE_DEBUG_MEMORY_ALLOCATOR 0
#define DXX_USE_EDITOR 0
#define DXX_USE_IPv6 0
#define DXX_USE_STEREOSCOPIC_RENDER 0
#define DXX_USE_MULTIPLAYER 1
#define DXX_USE_UDP 1
#define DXX_USE_TRACKER 1
#define DXX_USE_ADLMIDI 0
#define DXX_USE_SCREENSHOT_FORMAT_PNG 1
#define DXX_USE_SCREENSHOT_FORMAT_LEGACY 0
#define DXX_USE_SCREENSHOT 1
#define DXX_MAX_JOYSTICKS 8
#define DXX_MAX_AXES_PER_JOYSTICK 128
#define DXX_MAX_BUTTONS_PER_JOYSTICK 128
#define DXX_MAX_HATS_PER_JOYSTICK 4
#define DXX_USE_SDL_REDBOOK_AUDIO 0
#define DXX_USE_SDLIMAGE 1
#define DXX_USE_SDLMIXER 1
#define _XOPEN_SOURCE 700
#define DXX_HAVE_ATTRIBUTE_ERROR
#define dxx_compiler_attribute_error(M) __attribute__((__error__(M)))

#ifndef DXX_SCONF_NO_INCLUDES
dxx_compiler_attribute_error("must never be called")
void DXX_ALWAYS_ERROR_FUNCTION(const char *);
#endif
#define DXX_HAVE_BUILTIN_BSWAP
#define DXX_HAVE_BUILTIN_CONSTANT_P
#define DXX_CONSTANT_TRUE(E) (__builtin_constant_p((E)) && (E))
#define dxx_builtin_constant_p(A) __builtin_constant_p(A)
#define likely(A) __builtin_expect(!!(A), 1)
#define unlikely(A) __builtin_expect(!!(A), 0)
#define DXX_HAVE_CXX_BUILTIN_FILE_LINE 1
#define DXX_HAVE_BUILTIN_OBJECT_SIZE 1
#define DXX_BEGIN_COMPOUND_STATEMENT 
#define DXX_END_COMPOUND_STATEMENT 
#define dxx_compiler_attribute_always_inline() [[gnu::always_inline]]
#define dxx_compiler_attribute_cold [[gnu::cold]]
#define dxx_compiler_attribute_format_arg(A) __attribute__((format_arg(A)))
#define dxx_compiler_attribute_format_printf(A,B) __attribute__((format(printf,A,B)))
#define dxx_compiler_attribute_nonnull(...) __attribute__((nonnull __VA_ARGS__))
#define dxx_compiler_attribute_used __attribute__((used))
#define dxx_compiler_attribute_unused __attribute__((unused))
#define DXX_HAVE_CXX_DISAMBIGUATE_USING_NAMESPACE
#define DXX_HAVE_POISON_OVERWRITE 0
#define DXX_HAVE_POISON_VALGRIND 0
#define DXX_HAVE_POISON 0
#define DXX_PRI_size_type "zu"
/* 
For mingw32-gcc-5.4.0 and x86_64-pc-linux-gnu-gcc-5.4.0, gcc defines
`long operator-(T *, T *)`.

For mingw32, gcc reports a useless cast converting `long` to `int`.
For x86_64-pc-linux-gnu, gcc does not report a useless cast converting
`long` to `int`.

Various parts of the code take the difference of two pointers, then cast
it to `int` to be passed as a parameter to `snprintf` field width
conversion `.*`.  The field width conversion is defined to take `int`,
so the cast is necessary to avoid a warning from `-Wformat` on
x86_64-pc-linux-gnu, but is not necessary on mingw32.  However, the cast
causes a -Wuseless-cast warning on mingw32.  Resolve these conflicting
requirements by defining a macro that expands to `static_cast<int>` on
platforms where the cast is required and expands to nothing on platforms
where the cast is useless.
 */
#define DXX_ptrdiff_cast_int static_cast<int>
#define DXX_size_t_cast_unsigned_int static_cast<unsigned>
#define DXX_HAVE_STRCASECMP
#define DXX_HAVE_GETADDRINFO
#define DXX_HAVE_STRUCT_TIMESPEC

#endif /* BUILD_DXXSCONF_H_SEEN */
