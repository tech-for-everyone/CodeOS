/* iostream support for the freestanding Qt6 kernel link.
 *
 * Qt's static libs reference the std:: iostream vtables (from
 * QWidgetPrivate::flagsForDumping, which builds a std::stringstream but is
 * otherwise dead code).  Those vtables are only emitted by libstdc++, which
 * is not available for the x86_64-elf toolchain, so this TU explicitly
 * instantiates the char specializations against the host libstdc++ headers.
 *
 * The out-of-line non-template helpers (std::locale, std::ios_base,
 * basic_string, locale facets) are stubbed in cxx_support.cpp.
 */
#include <sstream>
#include <ostream>
#include <istream>
#include <locale>

namespace std {

/* ctype<char> is an explicit specialization in libstdc++; its static data
 * (locale::id) is normally provided by libstdc++.a which we don't link.
 * Define it here. */
locale::id ctype<char>::id;

template class basic_streambuf<char, char_traits<char>>;
template class basic_ios<char, char_traits<char>>;
template class basic_istream<char, char_traits<char>>;
template class basic_ostream<char, char_traits<char>>;
template class basic_iostream<char, char_traits<char>>;
template class __cxx11::basic_stringbuf<char, char_traits<char>, allocator<char>>;
template class __cxx11::basic_stringstream<char, char_traits<char>, allocator<char>>;
template class __cxx11::basic_istringstream<char, char_traits<char>, allocator<char>>;
template class __cxx11::basic_ostringstream<char, char_traits<char>, allocator<char>>;

template class num_get<char, istreambuf_iterator<char, char_traits<char>>>;
template class num_put<char, ostreambuf_iterator<char, char_traits<char>>>;
template class ctype<char>;

template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(unsigned short&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(unsigned int&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(long&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(unsigned long&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(bool&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(long long&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(unsigned long long&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(float&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(double&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(long double&);
template basic_istream<char, char_traits<char>>&
basic_istream<char, char_traits<char>>::_M_extract(void*&);

template basic_ostream<char, char_traits<char>>&
basic_ostream<char, char_traits<char>>::_M_insert<long>(long);
template basic_ostream<char, char_traits<char>>&
basic_ostream<char, char_traits<char>>::_M_insert<unsigned long>(unsigned long);
template basic_ostream<char, char_traits<char>>&
basic_ostream<char, char_traits<char>>::_M_insert<bool>(bool);
template basic_ostream<char, char_traits<char>>&
basic_ostream<char, char_traits<char>>::_M_insert<long long>(long long);
template basic_ostream<char, char_traits<char>>&
basic_ostream<char, char_traits<char>>::_M_insert<unsigned long long>(unsigned long long);
template basic_ostream<char, char_traits<char>>&
basic_ostream<char, char_traits<char>>::_M_insert<double>(double);
template basic_ostream<char, char_traits<char>>&
basic_ostream<char, char_traits<char>>::_M_insert<long double>(long double);
template basic_ostream<char, char_traits<char>>&
basic_ostream<char, char_traits<char>>::_M_insert<const void*>(const void*);

template ostream& operator<<(ostream&, char);
template ostream& operator<<(ostream&, unsigned char);
template ostream& operator<<(ostream&, signed char);

template istream& operator>>(istream&, char&);
template istream& operator>>(istream&, unsigned char&);
template istream& operator>>(istream&, signed char&);

template ostream& __ostream_insert<char, char_traits<char>>(ostream&, const char*, streamsize);

template streamsize
__copy_streambufs_eof<char, char_traits<char>>(basic_streambuf<char, char_traits<char>>*,
                                               basic_streambuf<char, char_traits<char>>*,
                                               bool&);

template istream& ws(istream&);

}
