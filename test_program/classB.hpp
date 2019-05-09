// Copyright (c) 2017-2018 Florian Wende (flwende@gmail.com)
//
// Distributed under the BSD 2-clause Software License
// (See accompanying file LICENSE)

#if !defined(CLASS_B_HPP)
#define CLASS_B_HPP

#include <iostream>
#include <cstdint>

namespace fw
{
    namespace extra
    {
        template <typename T>
        class B
        {
        private:
            const std::size_t dummy;
        public:
            B() : dummy(0), v(0), w(0) { ; }
            B(const T value) : dummy(1), v(value), w(value) { ; }
            B(const B& rhs) : dummy(rhs.dummy), v(rhs.v), w(rhs.w) { ; }

            #define MACRO(OP, IN_T)                     \
            inline void operator OP (const IN_T& rhs)   \
            {                                           \
                v OP rhs.v;                             \
                w OP rhs.w;                             \
            }
            MACRO(+=, B)
            MACRO(-=, B)
            #undef MACRO

            B& operator=(const T value)
            {
                v = value;
                w = value;
                return *this;    
            }

            T v;
        protected:
            const float x = 3;
        public:
            T w;

            const double len(const std::size_t idx, const B c = 2.0)
            {
                return 0.0;
            }

            template <typename TT>
            double len(const B<TT>& b, const std::size_t idx, const B c = 2.0)
            {
                return 0.0;
            }
        };

        template <typename T>
        std::ostream& operator<<(std::ostream& os, const B<T>& b)
        {
            os << "(" << b.v << "," << b.w << ")";
            return os;
        }
    }
}

#endif
