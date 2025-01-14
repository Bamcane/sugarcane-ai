#ifndef INCLUDE_STRING_H
#define INCLUDE_STRING_H

#include <cstddef>
#include <cstring>
#include <cstdlib>

class string
{
    char *str;
    size_t strsize;

public:
    string() :
        str(nullptr),
        strsize(0)
    {
    }

    string(const string& other) :
        str(nullptr),
        strsize(0)
    {
        copy(other);
    }

    string(const char *potherstr) :
        str(nullptr),
        strsize(0)
    {
        copy(potherstr);
    }
    
    ~string()
    {
        clear();
    }

    inline void copy(const char *potherstr)
    {
        if(!potherstr)
            return;

        strsize = strlen(potherstr) + 1;
        if(str)
            str = (char *) realloc(str, sizeof(char) * strsize);
        else
            str = (char *) malloc(sizeof(char) * strsize);

        memcpy(str, potherstr, strsize);
        str[strsize - 1] = '\0';
    }

    inline void append(const char *pappend)
    {
        if(!pappend)
            return;

        strsize += strlen(pappend);
        if(str)
            str = (char *) realloc(str, sizeof(char) * strsize);
        else
            str = (char *) malloc(sizeof(char) * strsize);

        memcpy(str + length(), pappend, strlen(pappend) + 1);
        str[strsize - 1] = '\0';
    }

    inline void moveto(void *pDest)
    {
        memmove(pDest, str, strsize);
        str = nullptr;
        strsize = 0;
    }

    inline void clear()
    {
        if(str)
            free(str);
        str = nullptr;
        strsize = 0;
    }

    inline const char* substr(size_t pos) const
    {
        return str + pos;
    }

    inline const char* substr(size_t pos)
    {
        return str + pos;
    }

    inline bool startswith(string test) const
    {
        if(test.length() > length())
            return false;
        return memcmp(str, test.str, test.length()) == 0;
    }

    inline bool startswith(string test)
    {
        if(test.length() > length())
            return false;
        return memcmp(str, test.str, test.length()) == 0;
    }

    inline bool endswith(string test) const
    {
        if(test.length() > length())
            return false;
        return memcmp(str + length() - test.length(), test.str, test.length()) == 0;
    }

    inline bool endswith(string test)
    {
        if(test.length() > length())
            return false;
        return memcmp(str + length() - test.length(), test.str, test.length()) == 0;
    }

    inline const char *c_str() const
    {
        return str;
    }

    inline const char *c_str()
    {
        return str;
    }

    inline size_t length() const
    {
        if(strsize == 0)
            return 0;

        return strsize - 1;
    }

    inline size_t length()
    {
        if(strsize == 0)
            return 0;

        return strsize - 1;
    }

    inline size_t size() const
    {
        return strsize;
    }

    inline size_t size()
    {
        return strsize;
    }

    inline bool operator==(const string& other) const
    {
        if(length() != other.length())
            return false;
        return memcmp(str, other.str, length()) == 0;
    }

    inline bool operator<(const string& other) const
    {
        if(length() < other.length())
            return true;
        if(length() > other.length())
            return false;
        return memcmp(str, other.str, length()) < 0;
    }

    inline string& operator=(const string& other)
    {
        copy(other);
        return *this;
    }

    inline string& operator+=(const string& other)
    {
        append(other);
        return *this;
    }

    inline operator const char*() const
    {
        return c_str();
    }

    inline operator const char*()
    {
        return c_str();
    }

    inline char operator[](int index) const
    {
        if(index >= strsize)
            return '\0';

        return str[index];
    }

    inline char operator[](int index)
    {
        if(index >= strsize)
            return '\0';

        return str[index];
    }
};
#endif // INCLUDE_STRING_H