/*
 * module without RF and Sensor.c
 *
 * Created: 21.02.2016 23:09:53
 * Author : Teymur
 */ 
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

static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x29};
	
//static uint8_t myip[4] = {211,100,0,29};
	static uint8_t myip[4] = {192,168,1,29};

#define WEBSERVER_VHOST "lenkeranistek.com"

#define MYWWWPORT 80

// Default gateway.
//static uint8_t gwip[4] = {211,100,0,1};
	static uint8_t gwip[4] = {192,168,1,1};

#define TRANS_NUM_GWMAC 1
static uint8_t gwmac[6]; 
static uint8_t otherside_www_ip[4]; // will be filled by dnslkup
//
//static char urlvarstr[21];
//
//
#define BUFFER_SIZE 650
static uint8_t buf[BUFFER_SIZE+1];
static uint8_t ip_sent=0;
static uint8_t web_client_attempts=0;
static uint8_t web_client_sendok=0;
static volatile uint8_t sec=0;
static volatile uint8_t cnt2step=0;
static int8_t dns_state=0;
static int8_t gw_arp_state=0;
uint8_t address=0;
//uint8_t sens[8];

uint16_t http200ok(void)
{
        return(fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n")));
}



uint16_t print_webpage(uint8_t *buf)
{
        uint16_t plen;
        char vstr[17];
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
		plen=fill_tcp_data_p(buf,plen,PSTR("\nAddress: "));
		itoa(address, vstr,10);
		plen=fill_tcp_data(buf,plen,vstr);
        plen=fill_tcp_data_p(buf,plen,PSTR("\ncheck result: <a href=http://tuxgraphics.org/cgi-bin/upld>http://tuxgraphics.org/cgi-bin/upld</a>"));
		
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


void ping_callback(uint8_t *ip){
        
        
}

void browserresult_callback(uint16_t webstatuscode,uint16_t datapos __attribute__((unused)), uint16_t len __attribute__((unused))){
        if (webstatuscode==200){
                web_client_sendok++;
        }
}
void arpresolver_result_callback(uint8_t *ip __attribute__((unused)),uint8_t transaction_number,uint8_t *mac){
        uint8_t i=0;
        if (transaction_number==TRANS_NUM_GWMAC){
                // copy mac address over:
                while(i<6){gwmac[i]=mac[i];i++;}
        }
}

int main(void){

        
        uint16_t dat_p,plen;
		DDRA = 0xFF;
		DDRC = 0x00;
		DDRD |= (1 << DDD0);

        _delay_loop_1(0); // 60us

        enc28j60Init(mymac);
        enc28j60clkout(2); // change clkout from 6.25MHz to 12.5MHz
        _delay_loop_1(0); // 60us
        
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
                        }
                        if (get_mac_with_arp_wait()==0 && gw_arp_state==1){
                                // done we have the mac address of the GW
                                gw_arp_state=2;
                        }
                        if (dns_state==0 && gw_arp_state==2){
                                if (!enc28j60linkup()) continue; // only for dnslkup_request we have to check if the link is up. 
                                sec=0;
                                dns_state=1;
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
                        if (ip_sent<4){
                                ip_sent++;
                                web_client_attempts++;
                                
                                client_browse_url(PSTR("/fromdevice/getip.php?pass=7s6d7&di=0F0-000-F0F-000-0F0&ordp=q79ty38jcld5ygbhgkl79l"),NULL,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
								//client_browse_url(PSTR("/input/VGvj9xAGQGcRxp7jO75J?private_key=9Yx6pdAY7YtgNXPEpPKo&test=5353"),urlvarstr,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
                       }
					   /*
					   PORTA = 0;
					   for (uint8_t i=0;i<8;i++)
					   {
						   PORTD = i+1;
						   PORTD |= (1<<PD0);
						   _delay_ms(1);
						   sens[i] = PINC;
					   }
					                      
                        */
						
                        continue;
                }
                if(dat_p==0){ // plen!=0
                        // check for incomming messages not processed
                        // as part of packetloop_arp_icmp_tcp, e.g udp messages
                        udp_client_check_for_dns_answer(buf,plen);
                        continue;
                }
                        
                if (strncmp("GET ",(char *)&(buf[dat_p]),4)!=0){
                        dat_p=http200ok();
                        dat_p=fill_tcp_data_p(buf,dat_p,PSTR("<h1>200 OK</h1>"));
                        goto SENDTCP;
                }
                if (strncmp("/ ",(char *)&(buf[dat_p+4]),2)==0){
                        dat_p=http200ok();
                        dat_p=print_webpage(buf);
                        goto SENDTCP;}
				if (strncmp("pass=7s6d7",(char *)&(buf[dat_p+5]),10)==0)
				{
					/*
					if (strncmp("address=0",(char *)&(buf[dat_p+16]),9)==0){
						if(strncmp("value=0",(char *)&(buf[dat_p+26]),7)==0){
							PORTA&=~(1<<PA0);
						}else{
							PORTA|=(1<<PA0);
						}
					}
					if (strncmp("address=1",(char *)&(buf[dat_p+16]),9)==0){
						if(strncmp("value=0",(char *)&(buf[dat_p+26]),7)==0){
							PORTA&=~(1<<PA1);
						}else{
							PORTA|=(1<<PA1);
						}
					}
					if (strncmp("address=2",(char *)&(buf[dat_p+16]),9)==0){
						if(strncmp("value=0",(char *)&(buf[dat_p+26]),7)==0){
							PORTA&=~(1<<PA2);
						}else{
							PORTA|=(1<<PA2);
						}
					}
					if (strncmp("address=3",(char *)&(buf[dat_p+16]),9)==0){
						if(strncmp("value=0",(char *)&(buf[dat_p+26]),7)==0){
							PORTA&=~(1<<PA3);
						}else{
							PORTA|=(1<<PA3);
						}
					}
					*/
					if (strncmp("address=",(char *)&(buf[dat_p+16]),8)==0){
						//address=10*atoi((char *)&buf[dat_p+24])+atoi((char *)&buf[dat_p+25]);
						address=atoi((char *)&buf[dat_p+24]);
						if (address<64)
						{
							PORTA=0x00;
							PORTA = address;
							if (strncmp("value=1",(char *)&(buf[dat_p+27]),7)==0)
							{
								PORTA|=(1<<PA6);
							}
							PORTA|=(1<<PA7);
						} 
						
					}
				}
                //
SENDTCP:
                www_server_reply(buf,dat_p); // send data

        }
        return (0);
}
