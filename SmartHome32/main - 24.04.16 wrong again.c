/*
 * SmartHome32.c
 *
 * Created: 28.09.2015 23:09:53
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

# define F_CPU 8000000UL


static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x29};
//static uint8_t myip[4] = {211,100,0,29};
static uint8_t myip[4] = {192,168,1,29};

#define WEBSERVER_VHOST "lenkeranistek.com"

#define MYWWWPORT 80

//static uint8_t gwip[4] = {211,100,0,1};
static uint8_t gwip[4] = {192,168,1,1};

#define TRANS_NUM_GWMAC 1
static uint8_t gwmac[6]; 
static uint8_t otherside_www_ip[4]; // will be filled by dnslkup
//
static char urlvarstr[21];
//
//
#define BUFFER_SIZE 650
static uint8_t buf[BUFFER_SIZE+1];
static uint8_t pingsrcip[4];
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
char sens_req[120];

uint16_t http200ok(void)
{
        return(fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n")));
}


// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage(uint8_t *buf)
{
        uint16_t plen;
        char vstr[17];
		
		char vstrt[17];
		char vstrh[17];
        uint8_t err;
        plen=http200ok();
        plen=fill_tcp_data_p(buf,plen,PSTR("<h2>web client status</h2>\n<pre>\n"));
        if (gw_arp_state==1){
                plen=fill_tcp_data_p(buf,plen,PSTR("waiting for GW "));
				/*
                mk_net_str(vstr,gwip,4,'.',10);
                plen=fill_tcp_data(buf,plen,vstr);
                plen=fill_tcp_data_p(buf,plen,PSTR(" to answer arp.\n"));
				*/
                return(plen);
        }
        if (dns_state==1){
                plen=fill_tcp_data_p(buf,plen,PSTR("waiting for DNS answer.\n"));
				/*
                err=dnslkup_get_error_info();
                plen=fill_tcp_data_p(buf,plen,PSTR("Error code: "));
                itoa(err,vstr,10);
                plen=fill_tcp_data(buf,plen,vstr);
                plen=fill_tcp_data_p(buf,plen,PSTR(" (0=no error)\n"));
				*/
                return(plen);
        }
        plen=fill_tcp_data_p(buf,plen,PSTR("Number of data uploads started by ping: "));
        itoa(web_client_attempts,vstr,10);
        plen=fill_tcp_data(buf,plen,vstr);
        plen=fill_tcp_data_p(buf,plen,PSTR("\nNumber successful data uploads to web: "));
        itoa(web_client_sendok,vstr,10);
        plen=fill_tcp_data(buf,plen,vstr);
		plen=fill_tcp_data_p(buf,plen,PSTR("\nClient start: "));
		itoa(start_web_client, vstr,10);
		plen=fill_tcp_data(buf,plen,vstr);
        plen=fill_tcp_data_p(buf,plen,PSTR("\ncheck result: <a href=http://tuxgraphics.org/cgi-bin/upld>http://tuxgraphics.org/cgi-bin/upld</a> <br>Temp:  "));
		PORTA = 0x00;
		PORTA &= (~(1<<PA5));
		PORTA |= 1;
		_delay_ms(50);
		temp = PINA;
		PORTA |= 2;
		_delay_ms(50);
		hum = PINA;
		itoa(temp,vstrt,10);
		itoa(hum,vstrh,10);
		plen=fill_tcp_data(buf,plen,vstrt);
		plen=fill_tcp_data_p(buf,plen,PSTR("\nHum:  "));
		plen=fill_tcp_data(buf,plen,vstrh);
        plen=fill_tcp_data_p(buf,plen,PSTR("\n</pre><br><hr>"));
        return(plen);
}



ISR(TIMER1_COMPA_vect){
	cnt2step++;
	if (cnt2step>50){
                cnt2step=0;
                sec++; // stepped every second
	}
}

void ping_callback(uint8_t *ip){
        uint8_t i=0;
        // trigger only first time in case we get many ping in a row:
		/*
        if (start_web_client==0){
                start_web_client=1;
                // save IP from where the ping came:
                while(i<4){
                        pingsrcip[i]=ip[i];
                        i++;
                }
        }
		*/
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
		start_web_client=1;
        
        uint16_t dat_p,plen;
        char str[20]; 
		DDRA = 0xFF;
		DDRD = 0x00;

        _delay_loop_1(0); // 60us

        enc28j60Init(mymac);
        enc28j60clkout(2); 
        _delay_loop_1(0); // 60us
        
        sei();

        enc28j60PhyWrite(PHLCON,0x476);

        DDRB|= (1<<DDB1); // LED, enable PB1, LED as output
        
        init_udp_or_www_server(mymac,myip);
        www_server_port(MYWWWPORT);
		
        // register to be informed about incomming ping:
        register_ping_rec_callback(&ping_callback);
		
		//client_browse_url(PSTR("/fromdevice/getip.php?pass=7s6d7&di=0F0-000-F0F-000-0F0&ordp=q79ty38jcld5ygbhgkl79l"),urlvarstr,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
		

        while(1){
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
                                if (sec > 1){
                                        dns_state=0;
                                }
                                continue;
                        }
                        //----------
                        if (start_web_client==1){
                                sec=0;
                                start_web_client=2;
                                web_client_attempts++;
                                for (uint8_t i=0; i<4; i++)
                                {
                                client_browse_url(PSTR("/fromdevice/getip.php?pass=7s6d7&di=0F0-000-F0F-000-0F0&ordp=q79ty38jcld5ygbhgkl79l"),NULL,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
								//client_browse_url(PSTR("/input/VGvj9xAGQGcRxp7jO75J?private_key=9Yx6pdAY7YtgNXPEpPKo&test=5353"),urlvarstr,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
                                }
                       }
                        
                        
                               start_web_client=0;
							   
						PORTA = 0x00;
						PORTA &= (~(1<<PA5));
						PORTA |= 1;
						_delay_ms(50);
						temp = PINA;
						PORTA |= 2;
						_delay_ms(50);
						hum = PINA;
						
						char vstrt[17];
						char vstrh[17];
						if (temp == 0xFF || hum == 0xFF)
						{
							strcpy(sens_req,"pass=7s6d7&di=0F0-000-F0F-000-0F0&ordp=q79ty38jcld5ygbhgkl79l&s1=ERROR&s2=ERROR&s3=0&s4=0&s5=0&s6=0&s7=0&s8=0");
						} 
						else
						{
							itoa(temp,vstrt,10);
							itoa(hum,vstrh,10);
							//strcpy(sens_req,"pass=7s6d7&di=0F0-000-F0F-000-0F0&ordp=q79ty38jcld5ygbhgkl79l&s1="+vstrt+"&s2="+vstrh+"&s3=0&s4=0&s5=0&s6=0&s7=0&s8=0");
							strcpy(sens_req,"pass=7s6d7&di=0F0-000-F0F-000-0F0&ordp=q79ty38jcld5ygbhgkl79l&s1=");
							strcat(sens_req,vstrt);
							strcat(sens_req,"&s2=");
							strcat(sens_req,vstrh);
							strcat(sens_req,"&s3=0&s4=0&s5=0&s6=0&s7=0&s8=0");
						}
						char req[200];
						strcpy(req, PSTR("/fromdevice/acceptdevice.php?"));
						strcat(req, sens_req);
						
						client_browse_url(req,NULL,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
                        
						
                        continue;
                }
                if(dat_p==0){ 
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
							PORTA|=(1<<PA5);
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
