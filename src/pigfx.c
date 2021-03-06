#include "peri.h"
#include "pigfx_config.h"
#include "uart.h"
#include "utils.h"
#include "c_utils.h"
#include "timer.h"
#include "console.h"
#include "gfx.h"
#include "framebuffer.h"
#include "irq.h"
#include "dma.h"
#include "nmalloc.h"
#include "ee_printf.h"
#include "prop.h"
#include "board.h"
#include "mbox.h"
#include "actled.h"
#include "emmc.h"
#include "mbr.h"
#include "fat.h"
#include "config.h"
#include "keyboard.h"
#include "../uspi/include/uspi/types.h"
#include "../uspi/include/uspi.h"

#define UART_BUFFER_SIZE 16384 /* 16k */


unsigned int led_status = 0;
unsigned char usbKeyboardFound = 0;
volatile unsigned int* pUART0_DR;
volatile unsigned int* pUART0_ICR;
volatile unsigned int* pUART0_IMSC;
volatile unsigned int* pUART0_FR;

volatile char* uart_buffer;
volatile char* uart_buffer_start;
volatile char* uart_buffer_end;
volatile char* uart_buffer_limit;

tPiGfxConfig PiGfxConfig;

extern unsigned int pheap_space;
extern unsigned int heap_sz;

extern unsigned char G_STARTUP_LOGO;


static void _heartbeat_timer_handler( __attribute__((unused)) unsigned hnd, 
                                      __attribute__((unused)) void* pParam, 
                                      __attribute__((unused)) void *pContext )
{
    if( led_status )
    {
        led_set(0);
        led_status = 0;
    } else
    {
        led_set(1);
        led_status = 1;
    }

    attach_timer_handler( HEARTBEAT_FREQUENCY, _heartbeat_timer_handler, 0, 0 );
}


void uart_fill_queue( __attribute__((unused)) void* data )
{
    while( !( *pUART0_FR & 0x10 ))
    {
        *uart_buffer_end++ = (char)( *pUART0_DR & 0xFF );

        if( uart_buffer_end >= uart_buffer_limit )
           uart_buffer_end = uart_buffer; 

        if( uart_buffer_end == uart_buffer_start )
        {
            uart_buffer_start++;
            if( uart_buffer_start >= uart_buffer_limit )
                uart_buffer_start = uart_buffer; 
        }
    }

    /* Clear UART0 interrupts */
    *pUART0_ICR = 0xFFFFFFFF;
}


void initialize_uart_irq()
{
    uart_buffer_start = uart_buffer_end = uart_buffer;
    uart_buffer_limit = &( uart_buffer[ UART_BUFFER_SIZE ] );

    pUART0_DR   = (volatile unsigned int*)UART0_DR;
    pUART0_IMSC = (volatile unsigned int*)UART0_IMSC;
    pUART0_ICR = (volatile unsigned int*)UART0_ICR;
    pUART0_FR   = (volatile unsigned int*)UART0_FR;

    *pUART0_IMSC = (1<<4) | (1<<7) | (1<<9); // Masked interrupts: RXIM + FEIM + BEIM (See pag 188 of BCM2835 datasheet)
    *pUART0_ICR = 0xFFFFFFFF; // Clear UART0 interrupts

    pIRQController->Enable_IRQs_2 = RPI_UART_INTERRUPT_IRQ;
    irq_attach_handler( 57, uart_fill_queue, 0 );
    enable_irq();
}


/** Sets the frame buffer with given width, height and bit depth.
 *   Other effects:
 *  	- font is set to 8x16
 *  	- tabulation is set to 8
 *  	- chars/sprites drawing mode is set to normal

 */
void initialize_framebuffer(unsigned int width, unsigned int height, unsigned int bpp)
{
    fb_release();

    unsigned char* p_fb=0;
    unsigned int fbsize;
    unsigned int pitch;

    unsigned int p_w = width;
    unsigned int p_h = height;
    unsigned int v_w = p_w;
    unsigned int v_h = p_h;

    fb_init( p_w, p_h, 
             v_w, v_h,
             bpp,
             (void*)&p_fb, 
             &fbsize, 
             &pitch );

    if (fb_set_xterm_palette() != 0)
    {
#if ENABLED(FRAMEBUFFER_DEBUG)
        cout("Set Palette failed"); cout_endl();
#endif
    }

    gfx_set_env( p_fb, v_w, v_h, bpp, pitch, fbsize );
    gfx_set_drawing_mode(drawingNORMAL);
    gfx_term_set_tabulation(8);
    gfx_term_set_font(8,16);
    gfx_clear();
}


void video_test(int maxloops)
{
    unsigned char ch='A';
    unsigned int row=0;
    unsigned int col=0;
    unsigned int term_cols, term_rows;
    gfx_get_term_size( &term_rows, &term_cols );

#if 0
    unsigned int t0 = time_microsec();
    for( row=0; row<1000000; ++row )
    {
        gfx_putc(0,col,ch);
    }
    t0 = time_microsec()-t0;
    cout("T: ");cout_d(t0);cout_endl();
    return;
#endif
#if 0
    while(1)
    {
        gfx_putc(row,col,ch);
        col = col+1;
        if( col >= term_cols )
        {
            usleep(100000);
            col=0;
            gfx_scroll_up(8);
        }
        ++ch;
        gfx_set_fg( ch );
    }
#endif
#if 1
    int count = maxloops;
    while(count >= 0)
    {
    	count--;
        gfx_putc(row,col,ch);
        col = col+1;
        if( col >= term_cols )
        {
            usleep(50000);
            col=0;
            row++;
            if( row >= term_rows )
            {
                row=term_rows-1;
                int i;
                for(i=0;i<10;++i)
                {
                    usleep(500000);
                    gfx_scroll_down(8);
                    gfx_set_bg( i );
                }
                usleep(1000000);
                gfx_clear();
                return;
            }

        }
        ++ch;
        gfx_set_fg( ch );
    }
#endif

#if 0
    while(1)
    {
        gfx_set_bg( RR );
        gfx_clear();
        RR = (RR+1)%16;
        usleep(1000000);
    }
#endif

}


void video_line_test(int maxloops)
{
    int x=-10; 
    int y=-10;
    int vx=1;
    int vy=0;

    gfx_set_fg( 15 );

    // what is current display size?
    unsigned int width=0, height=0;
    gfx_get_gfx_size( &width, &height );

    int count = maxloops;
    while(count >= 0)
    {
    	count --;

        // Render line
        gfx_line( width, height, x, y );

        usleep( 1000 );

        // Clear line
        gfx_swap_fg_bg();
        gfx_line( width, height, x, y );
        gfx_swap_fg_bg();

        x = x+vx;
        y = y+vy;
        
        if( x>700 )
        {
            x--;
            vx--;
            vy++;
        }
        if( y>500 )
        {
            y--;
            vx--;
            vy--;
        }
        if( x<-10 )
        {
            x++;
            vx++;
            vy--;
        }
        if( y<-10 )
        {
            y++;
            vx++;
            vy++;
        }

    }
}


void term_main_loop()
{
    ee_printf("Waiting for UART data (%d,8,N,1)\n",PiGfxConfig.uartBaudrate);

    /**/
    while( uart_buffer_start == uart_buffer_end )
    {
        timer_poll();       // ActLed working while waiting for data
#if RPI<4
        if (usbKeyboardFound) fUpdateKeyboardLeds();
#endif
    }
    /**/

    gfx_term_putstring( "\x1B[2J" );

    char strb[2] = {0,0};

    while(1)
    {
        if( !DMA_CHAN0_BUSY && uart_buffer_start != uart_buffer_end )
        {
            strb[0] = *uart_buffer_start++;
            if( uart_buffer_start >= uart_buffer_limit )
                uart_buffer_start = uart_buffer;

            
            if (PiGfxConfig.skipBackspaceEcho)
            {
                if( time_microsec()-last_backspace_t > 50000 )
                    backspace_n_skip=0;

                if( backspace_n_skip  > 0 )
                {
                    //ee_printf("Skip %c",strb[0]);
                    strb[0]=0; // Skip this char
                    backspace_n_skip--;
                    if( backspace_n_skip == 0)
                        strb[0]=0x7F; // Add backspace instead
                }
            }

            gfx_term_putstring( strb );
        }

        uart_fill_queue(0);
        
        timer_poll();
        
#if RPI<4
        if (usbKeyboardFound) fUpdateKeyboardLeds();
#endif
    }

}

void entry_point(unsigned int r0, unsigned int r1, unsigned int *atags)
{
    unsigned int boardRevision;
    board_t raspiBoard;
    
    //unused
    (void) r0;
    (void) r1;
    (void) atags;
    
    // Heap init
    nmalloc_set_memory_area( (unsigned char*)( pheap_space ), heap_sz );

    // UART buffer allocation
    uart_buffer = (volatile char*)nmalloc_malloc( UART_BUFFER_SIZE );
    
    // Get informations about the board we are booting
    boardRevision = prop_revision();
    raspiBoard = board_info(boardRevision);
    
/*#if RPI==4
    cout("Hello from the debug console\r\n");
    cout("Booting on Raspberry Pi ");
    cout(board_model(raspiBoard.model));
    cout(", ");
    cout(board_processor(raspiBoard.processor));
    cout("\r\n");
#endif*/
    
    // Where is the Act LED?
    led_init(raspiBoard);
    
    // Timers and heartbeat
    timers_init();
    attach_timer_handler( HEARTBEAT_FREQUENCY, _heartbeat_timer_handler, 0, 0 );
    
    initialize_framebuffer(640, 480, 8);
    

    gfx_term_putstring( "\x1B[2J" ); // Clear screen
    gfx_set_bg(BLUE);
    gfx_term_putstring( "\x1B[2K" ); // Render blue line at top
    gfx_set_fg(YELLOW);// bright yellow
    ee_printf(" ===  PiGFX %d.%d.%d  ===  Build %s\n", PIGFX_MAJVERSION, PIGFX_MINVERSION, PIGFX_BUILDVERSION, PIGFX_VERSION );
    gfx_term_putstring( "\x1B[2K" );
    ee_printf(" Copyright (c) 2016 Filippo Bergamasco\n\n");
    gfx_set_bg(BLACK);
    gfx_set_fg(DARKGRAY);

    // draw possible colors:
    // 0-15 are primary colors
    int color = 0;
    for (color = 0 ; color < 16 ; color++) {
   		gfx_set_bg(color);
   		ee_printf("%02x", color);
    }
    ee_printf("\n");

    // 16-223 are gradients
    int count = 0;
	for (  ; color <= 255-24 ; color++) {
		gfx_set_bg(color);
		ee_printf("%02x", color);
		count = (count + 1) % 36;
		if (count == 0)
			ee_printf("\n");
	}

	// 224-255 are gray scales
    for (  ; color <= 255 ; color++) {
		gfx_set_bg(color);
		ee_printf("%02x", color);
	}
	ee_printf("\n");

    /* informations
    gfx_set_bg(0);
    ee_printf("W: %d\nH: %d\nPitch: %d\nFont Width: %d, Height: %d\nChar bytes: %d\nFont Ints: %d, Remain: %d\n",
			ctx.W, ctx.H,
			ctx.Pitch,
			ctx.term.FONTWIDTH,
			ctx.term.FONTHEIGHT,
			ctx.term.FONTCHARBYTES,
			ctx.term.FONTWIDTH_INTS, ctx.term.FONTWIDTH_REMAIN);
    ee_printf("size: %d, bpp: %d\n", ctx.size, ctx.bpp);
	*/

    //video_test();
    //video_line_test();
    
    gfx_set_bg(BLACK);
    gfx_set_fg(GRAY);
    ee_printf("\nBooting on Raspberry Pi ");
    ee_printf(board_model(raspiBoard.model));
    ee_printf(", ");
    ee_printf(board_processor(raspiBoard.processor));
    ee_printf("\n");
    
    // Set default config
    setDefaultConfig();
    
    gfx_set_bg(BLUE);
    gfx_set_fg(YELLOW);
    ee_printf("Initializing filesystem:\n");
    gfx_set_bg(BLACK);
    gfx_set_fg(GRAY);
    // Try to load a config file
    lookForConfigFile();
    
    uart_init(PiGfxConfig.uartBaudrate);
    initialize_uart_irq();
    
#if RPI<4
    if (PiGfxConfig.useUsbKeyboard)
    {
        gfx_set_bg(BLUE);
        gfx_set_fg(YELLOW);
        ee_printf("Initializing USB:\n");
        gfx_set_bg(BLACK);
        gfx_set_fg(GRAY);

        if( USPiInitialize() )
        {
            ee_printf("Initialization OK!\n");
            ee_printf("Checking for keyboards: ");

            if ( USPiKeyboardAvailable () )
            {
                fInitUsbKeyboard(PiGfxConfig.keyboardLayout);
                USPiKeyboardRegisterKeyStatusHandlerRaw(KeyStatusHandlerRaw);
                gfx_set_fg(GREEN);
                usbKeyboardFound = 1;
                ee_printf("Keyboard found.\n");
                gfx_set_fg(GRAY);
            }
            else
            {
                gfx_set_fg(RED);
                ee_printf("No keyboard found.\n");
                gfx_set_fg(GRAY);
            }
        }
        else
        {
            gfx_set_fg(RED);
            ee_printf("USB initialization failed.\n");
        }
    }
#endif

    if (PiGfxConfig.showRC2014Logo)
    {
        gfx_set_drawing_mode(drawingTRANSPARENT);
        gfx_put_sprite( (unsigned char*)&G_STARTUP_LOGO, 0, 42 );
    }

    gfx_set_drawing_mode(drawingNORMAL);
    gfx_set_fg(GRAY);

    term_main_loop();
}
