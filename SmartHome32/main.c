/*********************************************
 * vim:sw=8:ts=8:si:et
 * To use the above modeline in vim you must have "set modeline" in your .vimrc
 * Author: Guido Socher
 * Copyright: GPL V2
 * See http://www.gnu.org/licenses/gpl.html
 *
 * Note: This software implements a web server and a web browser.
 * The web server is at "myip" and the browser tries to access 
 * the webserver at WEBSERVER_VHOST (tuxgraphics.org)
 *
 * Here is how to use this test software:
 * test_web_client.hex is a web client that uploads data to 
 * http://tuxgraphics.org/cgi-bin/upld when you ping it once
 * This is what you need to do:
 * - Change above the myip and gwip paramters 
 * - Compile and load the software
 * - ping the board once at myip from your computer (this will trigger the web-client)
 * - go to http://tuxgraphics.org/cgi-bin/upld and it will tell
 *   you from which IP address the ethernet board was ping-ed.
 *
 * Chip type           : Atmega168 or Atmega328 or Atmega644 with ENC28J60
 *********************************************/

#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <string.h>
// http://www.nongnu.org/avr-libc/changes-1.8.html:
#define __PROG_TYPES_COMPAT__
#include <avr/pgmspace.h>
#include "tux/ip_arp_udp_tcp.h"
#include "tux/websrv_help_functions.h"
#include "tux/enc28j60.h"
#include "tux/timeout.h"
#include "tux/net.h"
#include "tux/dnslkup.h"

#include "gsm/sim300.h"

// Please modify the following lines. mac and ip have to be unique
// in your local area network. You can not have the same numbers in
// two devices:
static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x29};
// how did I get the mac addr? Translate the first 3 numbers into ascii is: TUX
// This web server's own IP.
//static uint8_t myip[4] = {172,16,0,233};
static uint8_t myip[4] = {192,168,1,29};
//
// The name of the virtual host which you want to 
// contact at websrvip (hostname of the first portion of the URL):
#define WEBSERVER_VHOST "lenkeranistek.com"
//#define WEBSERVER_VHOST "data.sparkfun.com"
// listen port for tcp/www:
#define MYWWWPORT 80

// Default gateway. The ip address of your DSL router. It can be set to 
// the same as the web server in the case where there is no default GW to access the 
// web server (=web server is on the same lan as this host) 
static uint8_t gwip[4] = {192,168,1,1};
//static uint8_t gwip[4] = {172,16,0,2};
// --- there should not be any need to changes things below this line ---
#define TRANS_NUM_GWMAC 1
static uint8_t gwmac[6]; 
static uint8_t otherside_www_ip[4]; // will be filled by dnslkup
//
static char urlvarstr[21];
//
//
#define BUFFER_SIZE 650
static uint8_t buf[BUFFER_SIZE+1];
//static uint8_t pingsrcip[4];
static uint8_t start_web_client=0;
static uint8_t web_client_attempts=0;
static uint8_t web_client_sendok=0;
static volatile uint8_t sec=0;
static volatile uint8_t cnt2step=0;
static int8_t dns_state=0;
static int8_t gw_arp_state=0;

uint8_t address=0;

uint8_t temp=0;
uint8_t hum=0;

//uint8_t errors=0;
//uint8_t gsmerror=0;
//uint8_t simins=0;
//uint8_t msgerror=0;
//uint8_t msgref=0;
//uint8_t gsmretries =0;

uint16_t http200ok(void)
{
        return(fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n")));
}


// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage(uint8_t *buf)
{
        uint16_t plen;
        char vstr[17];
		
		//char vstrt[17];
		//char vstrh[17];
        uint8_t err;
        plen=http200ok();
        plen=fill_tcp_data_p(buf,plen,PSTR("<h2>web client status</h2>\n<pre>\n"));
        if (gw_arp_state==1){
                plen=fill_tcp_data_p(buf,plen,PSTR("waiting for GW "));
                mk_net_str(vstr,gwip,4,'.',10);
                plen=fill_tcp_data(buf,plen,vstr);
                plen=fill_tcp_data_p(buf,plen,PSTR(" to answer arp.\n"));
                return(plen);
        }
        if (dns_state==1){
                plen=fill_tcp_data_p(buf,plen,PSTR("waiting for DNS answer.\n"));
                err=dnslkup_get_error_info();
                plen=fill_tcp_data_p(buf,plen,PSTR("Error code: "));
                itoa(err,vstr,10);
                plen=fill_tcp_data(buf,plen,vstr);
                plen=fill_tcp_data_p(buf,plen,PSTR(" (0=no error)\n"));
                return(plen);
        }
        plen=fill_tcp_data_p(buf,plen,PSTR("Number of data uploads started by ping: "));
        // convert number to string:
        itoa(web_client_attempts,vstr,10);
        plen=fill_tcp_data(buf,plen,vstr);
        plen=fill_tcp_data_p(buf,plen,PSTR("\nNumber successful data uploads to web: "));
        // convert number to string:
        itoa(web_client_sendok,vstr,10);
        plen=fill_tcp_data(buf,plen,vstr);
		plen=fill_tcp_data_p(buf,plen,PSTR("\nClient start: "));
		itoa(start_web_client, vstr,10);
		plen=fill_tcp_data(buf,plen,vstr);
		//plen=fill_tcp_data_p(buf,plen,PSTR("\nGSM error: \nSIM300_OK 1\n  SIM300_INVALID_RESPONSE -1\n  SIM300_FAIL -2\n  SIM300_TIMEOUT -3\n"));
		//itoa(gsmerror,vstr,10);
		//plen=fill_tcp_data(buf,plen,vstr);
		//plen=fill_tcp_data_p(buf,plen,PSTR("\nGSM tries: "));
		//itoa(gsmretries,vstr,10);
		//plen=fill_tcp_data(buf,plen,vstr);
		//plen=fill_tcp_data_p(buf,plen,PSTR("\nSIM error: "));
		//itoa(simins,vstr,10);
		//plen=fill_tcp_data(buf,plen,vstr);
		//plen=fill_tcp_data_p(buf,plen,PSTR("\nMessage Error: "));
		//itoa(msgerror,vstr,10);
		//plen=fill_tcp_data(buf,plen,vstr);
		//plen=fill_tcp_data_p(buf,plen,PSTR("\nMessage reference: "));
		//itoa(msgref,vstr,10);
		//plen=fill_tcp_data(buf,plen,vstr);
        //PORTA = 0x00;
      //  PORTA &= (~(1<<PA5));
        //PORTA |= (1<<1);
      //  _delay_ms(50);
        //temp = PINA;
      //  PORTA |= (1<<2);
        //_delay_ms(50);
      //  hum = PINA;
        //itoa(temp,vstrt,10);
      //  itoa(hum,vstrh,10);
        //plen=fill_tcp_data(buf,plen,vstrt);
      //  plen=fill_tcp_data_p(buf,plen,PSTR("\nHum:  "));
        //plen=fill_tcp_data(buf,plen,vstrh);
        plen=fill_tcp_data_p(buf,plen,PSTR("\n</pre><br><hr>"));
        return(plen);
}


// called when TCNT2==OCR2A
// that is in 50Hz intervals
ISR(TIMER1_COMPA_vect){
	cnt2step++;
	if (cnt2step>50){
                cnt2step=0;
                sec++; // stepped every second
	}
}

// we were ping-ed by somebody, store the ip of the ping sender
// and trigger an upload to http://tuxgraphics.org/cgi-bin/upld
// This is just for testing and demonstration purpose
void ping_callback(uint8_t *ip){
        // trigger only first time in case we get many ping in a row:
        
}

// the __attribute__((unused)) is a gcc compiler directive to avoid warnings about unsed variables.
void browserresult_callback(uint16_t webstatuscode,uint16_t datapos __attribute__((unused)), uint16_t len __attribute__((unused))){
        if (webstatuscode==200){
                web_client_sendok++;
        }
}

// the __attribute__((unused)) is a gcc compiler directive to avoid warnings about unsed variables.
void arpresolver_result_callback(uint8_t *ip __attribute__((unused)),uint8_t transaction_number,uint8_t *mac){
        uint8_t i=0;
        if (transaction_number==TRANS_NUM_GWMAC){
                // copy mac address over:
                while(i<6){gwmac[i]=mac[i];i++;}
        }
}

int main(void){

        
        uint16_t dat_p,plen;
        //char str[20]; 
		DDRA = 0xFF;

        _delay_loop_1(0); // 60us

        /*initialize enc28j60*/
        enc28j60Init(mymac);
        enc28j60clkout(2); // change clkout from 6.25MHz to 12.5MHz
        _delay_loop_1(0); // 60us
		
		//gsmerror= SIM300Init();
		//gsmerror= SIM300Init();
		//gsmerror= SIM300Init();
		/*
		if (gsmerror==SIM300_FAIL)
		{
			gsmerror=SIM300Init();
			if (gsmerror==SIM300_FAIL)
			{
				gsmerror=SIM300Init();
				if (gsmerror==SIM300_FAIL)
				{
					gsmerror=SIM300Init();
					if (gsmerror==SIM300_FAIL)
					{
						gsmerror=SIM300Init();
					}
				}
			}
		}*/
		//simins=SIM300IsSIMInserted();
		//if(gsmerror==1 && simins==1){
			//msgerror = SIM300SendMsg("+994513131077","Test",&msgref);
		//}
        
        sei();

        /* Magjack leds configuration, see enc28j60 datasheet, page 11 */
        // LEDB=yellow LEDA=green
        //
        // 0x476 is PHLCON LEDA=links status, LEDB=receive/transmit
        // enc28j60PhyWrite(PHLCON,0b0000 0100 0111 01 10);
        enc28j60PhyWrite(PHLCON,0x476);

        DDRB|= (1<<DDB1); // LED, enable PB1, LED as output
        
        //init the web server ethernet/ip layer:
        init_udp_or_www_server(mymac,myip);
        www_server_port(MYWWWPORT);
		
        // register to be informed about incomming ping:
        register_ping_rec_callback(&ping_callback);
		
		//client_browse_url(PSTR("/fromdevice/getip.php?pass=7s6d7&di=0F0-000-F0F-000-0F0&ordp=q79ty38jcld5ygbhgkl79l"),urlvarstr,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
		start_web_client=1;

        while(1){
                // handle ping and wait for a tcp packet
                plen=enc28j60PacketReceive(BUFFER_SIZE, buf); 
                dat_p=packetloop_arp_icmp_tcp(buf,plen);
                if(plen==0){
                        // we are idle here trigger arp and dns stuff here
                        if (gw_arp_state==0){
                                // find the mac address of the gateway (e.g your dsl router).
                                get_mac_with_arp(gwip,TRANS_NUM_GWMAC,&arpresolver_result_callback);
                                gw_arp_state=1;
								//errors++;
                        }
                        if (get_mac_with_arp_wait()==0 && gw_arp_state==1){
                                // done we have the mac address of the GW
                                gw_arp_state=2;
                        }
                        if (dns_state==0 && gw_arp_state==2){
                                if (!enc28j60linkup()) continue; // only for dnslkup_request we have to check if the link is up. 
                                sec=0;
                                dns_state=1;
								//errors++;
                                dnslkup_request(buf,WEBSERVER_VHOST,gwmac);
                                continue;
                        }
                        if (dns_state==1 && dnslkup_haveanswer()){
                                dns_state=2;
                                dnslkup_get_ip(otherside_www_ip);
                        }
                        if (dns_state!=2){
                                // retry every minute if dns-lookup failed:
                                if (sec > 1){
                                        dns_state=0;
                                }
                                // don't try to use web client before
                                // we have a result of dns-lookup
                                continue;
                        }
                        //----------
						
						//if ((dns_state==1 || gw_arp_state==1) && errors>50)
						//{
							
						//}
						
                        if (start_web_client==1){
                                sec=0;
                                start_web_client=2;
                                web_client_attempts++;
                                
                                client_browse_url(PSTR("/fromdevice/getip.php?pass=7s6d7&di=0F0-000-F0F-000-0F0&ordp=q79ty38jcld5ygbhgkl79l"),urlvarstr,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
								//client_browse_url(PSTR("/input/VGvj9xAGQGcRxp7jO75J?private_key=9Yx6pdAY7YtgNXPEpPKo&test=5353"),urlvarstr,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
                       }
                        
                        
                       PORTA = 0x00;
                       PORTA &= (~(1<<PA5));
                       PORTA |= 1;
                       _delay_ms(50);
                       temp = PINA;
                       PORTA |= 2;
                       _delay_ms(50);
                       hum = PINA;
                        
						
                        continue;
                }
                if(dat_p==0){ // plen!=0
                        // check for incomming messages not processed
                        // as part of packetloop_arp_icmp_tcp, e.g udp messages
                        udp_client_check_for_dns_answer(buf,plen);
                        continue;
                }
                        
                if (strncmp("GET ",(char *)&(buf[dat_p]),4)!=0){
                        // head, post and other methods:
                        //
                        // for possible status codes see:
                        // http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
                        dat_p=http200ok();
                        dat_p=fill_tcp_data_p(buf,dat_p,PSTR("<h1>200 OK</h1>"));
                        goto SENDTCP;
                }
                if (strncmp("/ ",(char *)&(buf[dat_p+4]),2)==0){
                        dat_p=http200ok();
                        dat_p=print_webpage(buf);
                        goto SENDTCP;}
				if (strncmp("address=",(char *)&(buf[dat_p+16]),8)==0){
					//address=10*atoi((char *)&buf[dat_p+24])+atoi((char *)&buf[dat_p+25]);
					address=atoi((char *)&buf[dat_p+24]);
					if (address<64)
					{
						//PORTA=0x00;
						//PORTA = address;
						//PORTA|=(1<<PA5);
						if (strncmp("value=1",(char *)&(buf[dat_p+27]),7)==0)
						{
							PORTA|=(1<<address);
						}else{
							PORTA &= ~(1<<address);
						}
						//PORTA|=(1<<PA7);
					}
					
				}
                //
SENDTCP:
                www_server_reply(buf,dat_p); // send data

        }
        return (0);
}
