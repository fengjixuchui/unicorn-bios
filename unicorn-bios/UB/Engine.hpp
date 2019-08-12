/*******************************************************************************
 * The MIT License (MIT)
 * 
 * Copyright (c) 2019 Jean-David Gadina - www.xs-labs.com
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#ifndef UB_ENGINE_HPP
#define UB_ENGINE_HPP

#include <memory>
#include <algorithm>
#include <cstdint>
#include <vector>
#include <functional>
#include "UB/Registers.hpp"

namespace UB
{
    class Engine
    {
        public:
            
            static uint64_t getAddress( uint16_t segment, uint16_t offset );
            
            Engine( size_t memory );
            ~Engine( void );
            
            Engine( const Engine & o )              = delete;
            Engine( Engine && o )                   = delete;
            Engine & operator =( const Engine & o ) = delete;
            Engine & operator =( Engine && o )      = delete;
            
            size_t memory( void ) const;
            
            bool cf( void ) const;
            
            uint8_t ah( void ) const;
            uint8_t al( void ) const;
            uint8_t bh( void ) const;
            uint8_t bl( void ) const;
            uint8_t ch( void ) const;
            uint8_t cl( void ) const;
            uint8_t dh( void ) const;
            uint8_t dl( void ) const;
            
            uint16_t ax( void ) const;
            uint16_t bx( void ) const;
            uint16_t cx( void ) const;
            uint16_t dx( void ) const;
            uint16_t si( void ) const;
            uint16_t di( void ) const;
            uint16_t sp( void ) const;
            uint16_t bp( void ) const;
            uint16_t cs( void ) const;
            uint16_t ds( void ) const;
            uint16_t ss( void ) const;
            uint16_t es( void ) const;
            uint16_t fs( void ) const;
            uint16_t gs( void ) const;
            uint16_t ip( void ) const;
            
            uint32_t eax( void )    const;
            uint32_t ebx( void )    const;
            uint32_t ecx( void )    const;
            uint32_t edx( void )    const;
            uint32_t esi( void )    const;
            uint32_t edi( void )    const;
            uint32_t esp( void )    const;
            uint32_t ebp( void )    const;
            uint32_t eip( void )    const;
            uint32_t eflags( void ) const;
            
            void cf( bool value );
            
            void ah( uint8_t value );
            void al( uint8_t value );
            void bh( uint8_t value );
            void bl( uint8_t value );
            void ch( uint8_t value );
            void cl( uint8_t value );
            void dh( uint8_t value );
            void dl( uint8_t value );
            
            void ax( uint16_t value );
            void bx( uint16_t value );
            void cx( uint16_t value );
            void dx( uint16_t value );
            void si( uint16_t value );
            void di( uint16_t value );
            void sp( uint16_t value );
            void bp( uint16_t value );
            void cs( uint16_t value );
            void ds( uint16_t value );
            void ss( uint16_t value );
            void es( uint16_t value );
            void fs( uint16_t value );
            void gs( uint16_t value );
            void ip( uint16_t value );
            
            void eax( uint32_t value );
            void ebx( uint32_t value );
            void ecx( uint32_t value );
            void edx( uint32_t value );
            void esi( uint32_t value );
            void edi( uint32_t value );
            void esp( uint32_t value );
            void ebp( uint32_t value );
            void eip( uint32_t value );
            void eflags( uint32_t value );
            
            Registers registers( void ) const;
            
            bool running( void ) const;
            
            void onStart(               const std::function< void( void ) > f );
            void onStop(                const std::function< void( void ) > f );
            void onInterrupt(           const std::function< bool( uint32_t ) > handler );
            void onException(           const std::function< bool( const std::exception & ) > handler );
            void onInvalidMemoryAccess( const std::function< void( uint64_t, size_t ) > handler );
            void onValidMemoryAccess(   const std::function< void( uint64_t, size_t ) > handler );
            void beforeInstruction(     const std::function< void( uint64_t, const std::vector< uint8_t > & ) > handler );
            void afterInstruction(      const std::function< void( uint64_t, const Registers &, const std::vector< uint8_t > & ) > handler );
            
            std::vector< uint8_t > read( size_t address, size_t size );
            void                   write( size_t address, const std::vector< uint8_t > & bytes );
            void                   write( size_t address, const uint8_t * bytes, size_t size );
            
            bool start( size_t address );
            void stop( void );
            void waitUntilFinished( void ) const;
            
        private:
            
            class IMPL;
            std::unique_ptr< IMPL > impl;
    };
}

#endif /* UB_ENGINE_HPP */
