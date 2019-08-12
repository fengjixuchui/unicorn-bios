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

#include "UB/UI.hpp"
#include "UB/Screen.hpp"
#include "UB/String.hpp"
#include "UB/Casts.hpp"
#include "UB/Engine.hpp"
#include "UB/Casts.hpp"
#include "UB/Capstone.hpp"
#include "UB/Window.hpp"
#include "UB/Signal.hpp"
#include <mutex>
#include <optional>
#include <thread>
#include <functional>
#include <optional>
#include <iostream>
#include <condition_variable>
#include <csignal>

namespace UB
{
    class UI::IMPL
    {
        public:
            
            IMPL( Engine & engine );
            IMPL( const IMPL & o );
            IMPL( const IMPL & o, const std::lock_guard< std::recursive_mutex > & l );
            
            void _setupEngine( void );
            void _setupScreen( void );
            void _displayStatus( void );
            void _displayOutput( void );
            void _displayDebug( void );
            void _displayRegisters( void );
            void _displayRegisters( Window & window, const std::vector< std::pair< std::string, std::string > > & registers );
            void _displayFlags( void );
            void _displayStack( void );
            void _displayInstructions( void );
            void _displayDisassembly( void );
            void _displayMemory( void );
            void _memoryScrollUp( size_t n = 1 );
            void _memoryScrollDown( size_t n = 1 );
            void _memoryPageUp( void );
            void _memoryPageDown( void );
            
            bool                          _running;
            Mode                          _mode;
            Engine                      & _engine;
            StringStream                  _output;
            StringStream                  _debug;
            std::string                   _status;
            Color                         _statusColor;
            size_t                        _memoryOffset;
            size_t                        _memoryBytesPerLine;
            size_t                        _memoryLines;
            std::optional< std::string >  _memoryAddressPrompt;
            std::function< void( int ) >  _waitEnterOrSpaceKeyPress;
            mutable std::recursive_mutex  _rmtx;
    };
    
    UI::UI( Engine & engine ):
        impl( std::make_unique< IMPL >( engine ) )
    {}
    
    UI::UI( const UI & o ):
        impl( std::make_unique< IMPL >( *( o.impl ) ) )
    {}
    
    UI::UI( UI && o ) noexcept
    {
        std::lock_guard< std::recursive_mutex >( o.impl->_rmtx );
        
        this->impl = std::move( o.impl );
    }
    
    UI::~UI( void )
    {}
    
    UI & UI::operator =( UI o )
    {
        swap( *( this ), o );
        
        return *( this );
    }
    
    UI::Mode UI::mode( void ) const
    {
        std::lock_guard< std::recursive_mutex >( this->impl->_rmtx );
        
        return this->impl->_mode;
    }
    
    void UI::mode( Mode mode )
    {
        std::lock_guard< std::recursive_mutex >( this->impl->_rmtx );
        
        if( this->impl->_running )
        {
            throw std::runtime_error( "Cannot change the UI mode while UI is running" );
        }
        
        this->impl->_mode = mode;
    }
    
    void UI::run( void )
    {
        Mode mode;
        
        {
            std::lock_guard< std::recursive_mutex > l( this->impl->_rmtx );
            
            if( this->impl->_running )
            {
                return;
            }
            
            this->impl->_running = true;
            mode                 = this->impl->_mode;
            
            this->impl->_output = {};
            this->impl->_debug  = {};
            
            if( mode == Mode::Interactive )
            {
                this->impl->_setupScreen();
            }
            else
            {
                this->impl->_output.redirect( std::cout );
                this->impl->_debug.redirect(  std::cerr );
            }
        }
        
        {
            std::condition_variable_any cv;
            
            std::thread
            (
                [ & ]
                {
                    std::atomic< bool > exit( false );
                    
                    Signal::handle
                    (
                        SIGINT,
                        [ & ]( int sig )
                        {
                            if( sig == SIGINT )
                            {
                                exit = true;
                            }
                            
                            if( mode == Mode::Interactive )
                            {
                                Screen::shared().stop();
                            }
                        }
                    );
                    
                    if( mode == Mode::Interactive )
                    {
                        Screen::shared().start();
                    }
                    else
                    {
                        while( exit == false )
                        {
                            std::this_thread::yield();
                        }
                    }
                    
                    {
                        std::lock_guard< std::recursive_mutex > l( this->impl->_rmtx );
                        
                        this->impl->_running = false;
                        
                        cv.notify_all();
                    }
                }
            )
            .detach();
            
            {
                std::unique_lock< std::recursive_mutex > l( this->impl->_rmtx );
                
                cv.wait
                (
                    l,
                    [ & ]( void ) -> bool
                    {
                        return this->impl->_running == false;
                    }
                );
            }
        }
    }
    
    int UI::waitForUserResume( void )
    {
        bool                        keyPressed( false );
        std::condition_variable_any cv;
        
        if( this->mode() == Mode::Standard )
        {
            std::cout << "Emulation paused - Press [ENTER] to continue..." << std::endl;
            
            return getchar();
        }
        else
        {
            int pressed( 0 );
            
            {
                std::lock_guard< std::recursive_mutex > l( this->impl->_rmtx );
                std::string                             s;
                
                this->impl->_status                   = "Emulation paused - Press [ENTER] or [SPACE] to continue...";
                this->impl->_statusColor              = Color::yellow();
                this->impl->_waitEnterOrSpaceKeyPress =
                [ & ]( int key )
                {
                    std::lock_guard< std::recursive_mutex > l( this->impl->_rmtx );
                    
                    pressed    = key;
                    keyPressed = true;
                    
                    if( this->impl->_engine.running() )
                    {
                        this->impl->_status      = "Emulation running...";
                        this->impl->_statusColor = Color::green();
                    }
                    else
                    {
                        this->impl->_status      = "Emulation stopped";
                        this->impl->_statusColor = Color::red();
                    }
                    
                    cv.notify_all();
                };
            }
            
            {
                std::unique_lock< std::recursive_mutex > l( this->impl->_rmtx );
                
                cv.wait
                (
                    l,
                    [ & ]( void ) -> bool
                    {
                        return keyPressed;
                    }
                );
                
                return pressed;
            }
        }
    }
    
    StringStream & UI::output( void )
    {
        std::lock_guard< std::recursive_mutex > l( this->impl->_rmtx );
        
        return this->impl->_output;
    }
    
    StringStream & UI::debug( void )
    {
        std::lock_guard< std::recursive_mutex > l( this->impl->_rmtx );
        
        return this->impl->_debug;
    }
    
    void swap( UI & o1, UI & o2 )
    {
        std::lock( o1.impl->_rmtx, o2.impl->_rmtx );
        
        {
            std::lock_guard< std::recursive_mutex > l1( o1.impl->_rmtx, std::adopt_lock );
            std::lock_guard< std::recursive_mutex > l2( o2.impl->_rmtx, std::adopt_lock );
            
            using std::swap;
            
            swap( o1.impl, o2.impl );
        }
    }
    
    UI::IMPL::IMPL( Engine & engine ):
        _running(            false ),
        _mode(               Mode::Interactive ),
        _engine(             engine ),
        _status(             "Emulation not running" ),
        _statusColor(        Color::red() ),
        _memoryOffset(       0x7C00 ),
        _memoryBytesPerLine( 0 ),
        _memoryLines(        0 )
    {
        this->_setupEngine();
    }
    
    UI::IMPL::IMPL( const IMPL & o ):
        IMPL( o, std::lock_guard< std::recursive_mutex >( o._rmtx ) )
    {}
    
    UI::IMPL::IMPL( const IMPL & o, const std::lock_guard< std::recursive_mutex > & l ):
        _running(            false ),
        _mode(               o._mode ),
        _engine(             o._engine ),
        _output(             o._output.string() ),
        _debug(              o._debug.string() ),
        _status(             "Emulation not running" ),
        _statusColor(        Color::red() ),
        _memoryOffset(       o._memoryOffset ),
        _memoryBytesPerLine( o._memoryBytesPerLine ),
        _memoryLines(        o._memoryLines )
    {
        ( void )l;
        
        this->_setupEngine();
    }
    
    void UI::IMPL::_setupEngine( void )
    {
        this->_engine.onStart
        (
            [ & ]
            {
                std::lock_guard< std::recursive_mutex > l( this->_rmtx );
                
                this->_status      = "Emulation running...";
                this->_statusColor = Color::green();
            }
        );
        
        this->_engine.onStop
        (
            [ & ]
            {
                std::lock_guard< std::recursive_mutex > l( this->_rmtx );
                
                this->_status      = "Emulation stopped";
                this->_statusColor = Color::red();
            }
        );
    }
    
    void UI::IMPL::_setupScreen( void )
    {
        Screen::shared().onUpdate
        (
            [ & ]( void )
            {
                if( Screen::shared().width() < 50 || Screen::shared().height() < 30 )
                {
                    Screen::shared().clear();
                    Screen::shared().print( Color::red(), "Screen too small..." );
                    
                    return;
                }
                
                this->_displayRegisters();
                this->_displayFlags();
                this->_displayStack();
                this->_displayInstructions();
                this->_displayDisassembly();
                this->_displayMemory();
                this->_displayOutput();
                this->_displayDebug();
                this->_displayStatus();
            }
        );
        
        Screen::shared().onKeyPress
        (
            [ & ]( int key )
            {
                if( key == 'q' )
                {
                    Screen::shared().stop();
                }
                else if( key == 'm' )
                {
                    if( this->_memoryAddressPrompt.has_value() )
                    {
                        this->_memoryAddressPrompt = {};
                    }
                    else
                    {
                        this->_memoryAddressPrompt = "";
                    }
                }
                else if( ( key == 10 || key == 13 ) && this->_memoryAddressPrompt.has_value() )
                {
                    std::string prompt( this->_memoryAddressPrompt.value() );
                    
                    if( prompt.length() > 0 )
                    {
                        this->_memoryOffset = String::fromHex< size_t >( prompt );
                    }
                    
                    this->_memoryAddressPrompt = {};
                }
                else if( key == 10 || key == 13 || key == 0x20 )
                {
                    std::lock_guard< std::recursive_mutex > l( this->_rmtx );
                    
                    if( this->_waitEnterOrSpaceKeyPress != nullptr )
                    {
                        this->_waitEnterOrSpaceKeyPress( key );
                    }
                    
                    this->_waitEnterOrSpaceKeyPress = {};
                }
                else if( key == 127 && this->_memoryAddressPrompt.has_value() )
                {
                    std::string prompt( this->_memoryAddressPrompt.value() );
                    
                    if( prompt.length() > 0 )
                    {
                        this->_memoryAddressPrompt = prompt.substr( 0, prompt.length() - 1 );
                    }
                }
                else
                {
                    if( this->_memoryAddressPrompt.has_value() && key >= 0 && key < 128 && isprint( key ) )
                    {
                        this->_memoryAddressPrompt = this->_memoryAddressPrompt.value() + numeric_cast< char >( key );
                    }
                    else if( key == 'a' )
                    {
                        this->_memoryScrollUp();
                    }
                    else if( key == 's' )
                    {
                        this->_memoryScrollDown();
                    }
                    else if( key == 'd' )
                    {
                        this->_memoryPageUp();
                    }
                    else if( key == 'f' )
                    {
                        this->_memoryPageDown();
                    }
                    else if( key == 'g' )
                    {
                        this->_memoryOffset = 0;
                    }
                }
            }
        );
    }
    
    void UI::IMPL::_displayStatus( void )
    {
        size_t x(      0 );
        size_t y(      Screen::shared().height() - 3 );
        size_t width(  Screen::shared().width() );
        size_t height( 3 );
        Window win( x, y, width, height );
        
        {
            std::lock_guard< std::recursive_mutex > l( this->_rmtx );
            
            win.box();
            win.move( 2, 1 );
            win.print( this->_statusColor, this->_status );
        }
        
        Screen::shared().refresh();
        win.move( 0, 0 );
        win.refresh();
    }
    
    void UI::IMPL::_displayOutput( void )
    {
        size_t x(      0 );
        size_t y(      21 + ( ( Screen::shared().height() - 21 ) / 2 ) );
        size_t width(  Screen::shared().width() / 2 );
        size_t height( ( ( Screen::shared().height() - 21 ) / 2 ) - 2 );
        Window win( x, y, width, height );
        
        win.box();
        win.move( 2, 1 );
        win.print( Color::blue(), "Output:" );
        win.move( 1, 2 );
        win.addHorizontalLine( width - 2 );
        
        y = 3;
        
        {
            std::vector< std::string > lines;
            std::vector< std::string > display;
            size_t                     maxLines( numeric_cast< size_t >( height ) - 4 );
            size_t                     max( 80 );
            
            {
                std::lock_guard< std::recursive_mutex > l( this->_rmtx );
                
                lines = String::lines( this->_output.string() );
            }
            
            if( numeric_cast< size_t >( width - 4 ) < max )
            {
                max = numeric_cast< size_t >( width - 4 );
            }
            
            for( std::string s: lines )
            {
                while( s.length() > max )
                {
                    display.push_back( s.substr( 0, max ) );
                    
                    s = s.substr( max );
                }
                
                display.push_back( s );
            }
            
            if( display.size() > maxLines )
            {
                display = std::vector< std::string >( display.end() - numeric_cast< ssize_t >( maxLines ), display.end() );
            }
            
            for( const auto & s: display )
            {
                win.move( 2, y++ );
                win.print( s );
            }
        }
        
        Screen::shared().refresh();
        win.move( 0, 0 );
        win.refresh();
    }
    
    void UI::IMPL::_displayDebug( void )
    {
        size_t x(      Screen::shared().width() / 2 );
        size_t y(      21 + ( ( Screen::shared().height() - 21 ) / 2 ) );
        size_t width(  Screen::shared().width() / 2 );
        size_t height( ( ( Screen::shared().height() - 21 ) / 2 ) - 2 );
        Window win( x, y, width, height );
        
        win.box();
        win.move( 2, 1 );
        win.print( Color::blue(), "Debug:" );
        win.move( 1, 2 );
        win.addHorizontalLine( width - 2 );
        
        y = 3;
        
        {
            std::vector< std::string > lines;
            size_t                     maxLines( numeric_cast< size_t >( height ) - 4 );
            
            {
                std::lock_guard< std::recursive_mutex > l( this->_rmtx );
                
                lines = String::lines( this->_debug.string() );
            }
            
            if( lines.size() > maxLines )
            {
                lines = std::vector< std::string >( lines.end() - numeric_cast< ssize_t >( maxLines ), lines.end() );
            }
            
            for( const auto & s: lines )
            {
                win.move( 2, y++ );
                win.print( Color::magenta(), s );
            }
        }
        
        Screen::shared().refresh();
        win.move( 0, 0 );
        win.refresh();
    }
    
    void UI::IMPL::_displayRegisters( void )
    {
        size_t x(      0 );
        size_t y(      0 );
        size_t width(  54 );
        size_t height( 21 );
        Window win( x, y, width, height );
        
        if( Screen::shared().width() < x + width )
        {
            return;
        }
        
        win.box();
        win.move( 2, 1 );
        win.print( Color::blue(), "CPU Registers:" );
        win.move( 1, 2 );
        win.addHorizontalLine( width - 2 );
        
        y = 3;
        
        {
            Registers   reg( this->_engine.registers() );
            std::string ah( String::toHex( reg.ah() ) );
            std::string al( String::toHex( reg.al() ) );
            std::string bh( String::toHex( reg.bh() ) );
            std::string bl( String::toHex( reg.bl() ) );
            std::string ch( String::toHex( reg.ch() ) );
            std::string cl( String::toHex( reg.cl() ) );
            std::string dh( String::toHex( reg.dh() ) );
            std::string dl( String::toHex( reg.dl() ) );
            std::string ax( String::toHex( reg.ax() ) );
            std::string bx( String::toHex( reg.bx() ) );
            std::string cx( String::toHex( reg.cx() ) );
            std::string dx( String::toHex( reg.dx() ) );
            std::string si( String::toHex( reg.si() ) );
            std::string di( String::toHex( reg.di() ) );
            std::string sp( String::toHex( reg.sp() ) );
            std::string bp( String::toHex( reg.bp() ) );
            std::string cs( String::toHex( reg.cs() ) );
            std::string ds( String::toHex( reg.ds() ) );
            std::string ss( String::toHex( reg.ss() ) );
            std::string es( String::toHex( reg.es() ) );
            std::string fs( String::toHex( reg.fs() ) );
            std::string gs( String::toHex( reg.gs() ) );
            std::string ip( String::toHex( reg.ip() ) );
            std::string eax( String::toHex( reg.eax() ) );
            std::string ebx( String::toHex( reg.ebx() ) );
            std::string ecx( String::toHex( reg.ecx() ) );
            std::string edx( String::toHex( reg.edx() ) );
            std::string esi( String::toHex( reg.esi() ) );
            std::string edi( String::toHex( reg.edi() ) );
            std::string esp( String::toHex( reg.esp() ) );
            std::string ebp( String::toHex( reg.ebp() ) );
            std::string eip( String::toHex( reg.eip() ) );
            uint32_t    eflags32( reg.eflags() );
            std::string eflags( String::toHex( eflags32 ) );
            
            
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "EAX", eax }, { "AX", ax }, { "AH", ah }, { "AL", al } } );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "EBX", ebx }, { "BX", bx }, { "BH", bh }, { "BL", bl } } );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "ECX", ecx }, { "CX", cx }, { "CH", ch }, { "CL", cl } } );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "EDX", edx }, { "DX", dx }, { "DH", dh }, { "DL", dl } } );
            win.move( 1, y++ );
            win.addHorizontalLine( width - 2 );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "ESI", esi }, { "SI", si } } );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "EDI", edi }, { "DI", di } } );
            win.move( 1, y++ );
            win.addHorizontalLine( width - 2 );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "EBP", ebp }, { "BP", bp } } );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "ESP", esp }, { "SP", sp } } );
            win.move( 1, y++ );
            win.addHorizontalLine( width - 2 );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "CS", cs }, { "DS", ds }, { "SS", ss } } );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "ES", es }, { "FS", fs }, { "GS", gs } } );
            win.move( 1, y++ );
            win.addHorizontalLine( width - 2 );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "EIP", eip }, { "IP", ip } } );
            win.move( 1, y++ );
            win.addHorizontalLine( width - 2 );
            win.move( 2, y++ );
            this->_displayRegisters( win, { { "EFLAGS", eflags } } );
            win.move( 1, y++ );
        }
        
        Screen::shared().refresh();
        win.move( 0, 0 );
        win.refresh();
    }
    
    void UI::IMPL::_displayRegisters( Window & window, const std::vector< std::pair< std::string, std::string > > & registers )
    {
        for( size_t i = 0; i < registers.size(); i++ )
        {
            window.print( Color::cyan(), registers[ i ].first );
            window.print( ": " );
            window.print( Color::yellow(), registers[ i ].second );
            
            if( i < registers.size() - 1 && registers.size() > 1 )
            {
                window.print( " | " );
            }
        }
    }
    
    void UI::IMPL::_displayFlags( void )
    {
        size_t x(      54 );
        size_t y(      0 );
        size_t width(  36 );
        size_t height( 21 );
        Window win( x, y, width, height );
        
        if( Screen::shared().width() < x + width )
        {
            return;
        }
        
        win.box();
        win.move( 2, 1 );
        win.print( Color::blue(), "CPU Flags:" );
        win.move( 1, 2 );
        win.addHorizontalLine( width - 2 );
        
        y = 3;
        
        {
            uint32_t                                      eflags( this->_engine.registers().eflags() );
            std::vector< std::pair< std::string, bool > > flags;
            
            flags.push_back( { "Carry",                     ( eflags & ( 1 <<  0 ) ) != 0 } );
            flags.push_back( { "Parity",                    ( eflags & ( 1 <<  2 ) ) != 0 } );
            flags.push_back( { "Adjust",                    ( eflags & ( 1 <<  4 ) ) != 0 } );
            flags.push_back( { "Zero",                      ( eflags & ( 1 <<  6 ) ) != 0 } );
            flags.push_back( { "Sign",                      ( eflags & ( 1 <<  7 ) ) != 0 } );
            flags.push_back( { "Trap",                      ( eflags & ( 1 <<  8 ) ) != 0 } );
            flags.push_back( { "Interrupt enable",          ( eflags & ( 1 <<  9 ) ) != 0 } );
            flags.push_back( { "Direction",                 ( eflags & ( 1 << 10 ) ) != 0 } );
            flags.push_back( { "Overflow",                  ( eflags & ( 1 << 11 ) ) != 0 } );
            flags.push_back( { "Resume",                    ( eflags & ( 1 << 16 ) ) != 0 } );
            flags.push_back( { "Virtual 8086",              ( eflags & ( 1 << 17 ) ) != 0 } );
            flags.push_back( { "Alignment check",           ( eflags & ( 1 << 18 ) ) != 0 } );
            flags.push_back( { "Virtual interrupt",         ( eflags & ( 1 << 19 ) ) != 0 } );
            flags.push_back( { "Virtual interrupt pending", ( eflags & ( 1 << 20 ) ) != 0 } );
            flags.push_back( { "CPUID",                     ( eflags & ( 1 << 21 ) ) != 0 } );
            
            for( const auto & p: flags )
            {
                win.move( 2, y );
                win.print( Color::cyan(), p.first );
                win.print( ":" );
                win.move( 31, y );
                
                if( p.second )
                {
                    win.print( Color::green(), "Yes" );
                }
                else
                {
                    win.print( Color::red(), " No" );
                }
                
                y++;
            }
            
            win.move( 1, y++ );
            win.addHorizontalLine( width - 2 );
            win.move( 2, y++ );
            win.print( Color::yellow(), String::toBinary( eflags ) );
        }
        
        Screen::shared().refresh();
        win.move( 0, 0 );
        win.refresh();
    }
    
    void UI::IMPL::_displayStack( void )
    {
        size_t x(      54 + 36 );
        size_t y(      0 );
        size_t width(  30 );
        size_t height( 21 );
        Window win( x, y, width, height );
        
        if( Screen::shared().width() < x + width )
        {
            return;
        }
        
        win.box();
        win.move( 2, 1 );
        win.print( Color::blue(), "Stack Frame:" );
        win.move( 1, 2 );
        win.addHorizontalLine( width - 2 );
        
        y = 3;
        
        {
            Registers reg( this->_engine.registers() );
            uint16_t  ss( reg.ss() );
            uint64_t  bp( Engine::getAddress( ss, reg.bp() ) );
            uint64_t  sp( Engine::getAddress( ss, reg.sp() ) );
            
            std::vector< std::pair< uint64_t, uint16_t > > frame;
            
            while( sp + 1 < bp )
            {
                std::vector< uint8_t > data( this->_engine.read( sp, 2 ) );
                uint16_t               i( 0 );
                
                if( data.size() != 2 )
                {
                    break;
                }
                
                i   = data[ 0 ];
                i <<= 8;
                i  |= data[ 1 ];
                
                frame.push_back( { sp, i } );
                
                sp += 2;
            }
            
            if( frame.size() == 0 )
            {
                for( size_t i = y; i < height - 1; i++ )
                {
                    win.move( 2, y++ );
                    
                    for( size_t j = 2; j < width - 2; j++ )
                    {
                        win.print( Color::red(), "." );
                    }
                }
            }
            else
            {
                for( auto p: frame )
                {
                    if( y == height - 1 )
                    {
                        break;
                    }
                    
                    win.move( 2, y++ );
                    win.print( Color::cyan(), String::toHex( p.first ) );
                    win.print( ": " );
                    win.print( Color::yellow(), String::toHex( p.second ) );
                }
            }
        }
        
        Screen::shared().refresh();
        win.move( 0, 0 );
        win.refresh();
    }
    
    void UI::IMPL::_displayInstructions( void )
    {
        size_t x(      54 + 36 + 30 );
        size_t y(      0 );
        size_t width(  56 );
        size_t height( 21 );
        Window win( x, y, width, height );
        
        if( Screen::shared().width() < x + width )
        {
            return;
        }
        
        win.box();
        win.move( 2, 1 );
        win.print( Color::blue(), "Instructions:" );
        win.move( 1, 2 );
        win.addHorizontalLine( width - 2 );
        
        y = 3;
        
        try
        {
            uint64_t                                             ip( this->_engine.registers().eip() );
            std::vector< uint8_t >                               bytes( this->_engine.read( ip, 512 ) );
            std::vector< std::pair< std::string, std::string > > instructions( Capstone::instructions( bytes, ip ) );
            
            for( const auto & p: instructions )
            {
                if( y == height - 1 )
                {
                    break;
                }
                
                win.move( 2, y++ );
                win.print( Color::cyan(), p.first );
                win.print( ": " );
                win.print( Color::yellow(), p.second );
            }
        }
        catch( ... )
        {}
        
        Screen::shared().refresh();
        win.move( 0, 0 );
        win.refresh();
    }
    
    void UI::IMPL::_displayDisassembly( void )
    {
        size_t x( 54 + 36 + 30 + 56 );
        
        if( Screen::shared().width() < x + 50 )
        {
            return;
        }
        
        {
            size_t y(      0 );
            size_t width(  Screen::shared().width() - x );
            size_t height( 21 );
            Window win( x, y, width, height );
            
            win.box();
            win.move( 2, 1 );
            win.print( Color::blue(), "Disassembly:" );
            win.move( 1, 2 );
            win.addHorizontalLine( width - 2 );
            
            y = 3;
            
            try
            {
                uint64_t                                             ip( this->_engine.registers().eip() );
                std::vector< uint8_t >                               bytes( this->_engine.read( ip, 512 ) );
                std::vector< std::pair< std::string, std::string > > instructions( Capstone::disassemble( bytes, ip ) );
                
                for( const auto & p: instructions )
                {
                    if( y == height - 1 )
                    {
                        break;
                    }
                    
                    win.move( 2, y++ );
                    win.print( Color::cyan(), p.first );
                    win.print( ": " );
                    win.print( Color::yellow(), p.second );
                }
            }
            catch( ... )
            {}
            
            Screen::shared().refresh();
            win.move( 0, 0 );
            win.refresh();
        }
    }
    
    void UI::IMPL::_displayMemory( void )
    {
        size_t x(      0 );
        size_t y(      21 );
        size_t width(  Screen::shared().width() );
        size_t height( ( Screen::shared().height() - y ) / 2 );
        Window win( x, y, width, height );
        
        win.box();
        win.move( 2, 1 );
        win.print( Color::blue(), "Memory:" );
        win.move( 1, 2 );
        win.addHorizontalLine( width - 2 );
        
        y = 3;
        
        if( this->_memoryAddressPrompt.has_value() )
        {
            win.move( 2, 3 );
            win.print( Color::yellow(), "Enter a memory address:" );
            win.move( 2, 4 );
            win.print( Color::cyan(), this->_memoryAddressPrompt.value() );
        }
        else
        {
            size_t cols(  Screen::shared().width()  - 4 );
            size_t lines( numeric_cast< size_t >( height ) - 4 );
            
            this->_memoryBytesPerLine = ( cols / 4 ) - 5;
            this->_memoryLines        = lines;
            
            {
                size_t                 size(   this->_memoryBytesPerLine * lines );
                size_t                 offset( this->_memoryOffset );
                std::vector< uint8_t > mem(    this->_engine.read( offset, size ) );
                
                for( size_t i = 0; i < mem.size(); i++ )
                {
                    if( i % this->_memoryBytesPerLine == 0 )
                    {
                        win.move( 2, y++ );
                        win.print( Color::cyan(), "%016X: ", offset );
                        
                        offset += this->_memoryBytesPerLine;
                    }
                    
                    win.print( Color::yellow(), "%02X ", numeric_cast< int >( mem[ i ] ) );
                }
                
                y = 3;
                
                win.move( ( this->_memoryBytesPerLine * 3 ) + 4 + 16, y );
                win.addVerticalLine( lines );
                
                for( size_t i = 0; i < mem.size(); i++ )
                {
                    char c = static_cast< char >( mem[ i ] );
                    
                    if( i % this->_memoryBytesPerLine == 0 )
                    {
                        win.move( ( this->_memoryBytesPerLine * 3 ) + 4 + 18, y++ );
                    }
                    
                    if( isprint( c ) == false || isspace( c ) )
                    {
                        win.print( Color::blue(), "." );
                    }
                    else
                    {
                        win.print( "%c", c );
                    }
                }
            }
        }
        
        Screen::shared().refresh();
        win.move( 0, 0 );
        win.refresh();
    }
    
    void UI::IMPL::_memoryScrollUp( size_t n )
    {
        if( this->_memoryOffset > ( this->_memoryBytesPerLine * n ) )
        {
            this->_memoryOffset -= ( this->_memoryBytesPerLine * n );
        }
        else
        {
            this->_memoryOffset = 0;
        }
    }
    
    void UI::IMPL::_memoryScrollDown( size_t n )
    {
        if( ( this->_memoryOffset + ( this->_memoryBytesPerLine * n ) ) < this->_engine.memory() )
        {
            this->_memoryOffset += ( this->_memoryBytesPerLine * n );
        }
    }
    
    void UI::IMPL::_memoryPageUp( void )
    {
        this->_memoryScrollUp( this->_memoryLines );
    }
    
    void UI::IMPL::_memoryPageDown( void )
    {
        this->_memoryScrollDown( this->_memoryLines );
    }
}
