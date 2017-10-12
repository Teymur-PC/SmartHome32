/*
 * SmartHome32.c
 *
 * Created: 15.11.2015 17:04:04
 * Author : Teymur
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>
//tux
#define __PROG_TYPES_COMPAT__
#include "tux/ip_arp_udp_tcp.h"
#include "tux/websrv_help_functions.h"
#include "tux/enc28j60.h"
#include "tux/timeout.h"
#include "tux/net.h"
#include "tux/dnslkup.h"
#include "lcd/lcd_hd44780_avr.h"


//variables  212.47.142.179
#define WEBSERVER_VHOST "data.sparkfun.com"
#define MYWWWPORT 80
static uint8_t myip[4] = {192,168,1,29};
static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x29};
static uint8_t gwip[4] = {192,168,1,1};

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
static uint8_t pingsrcip[4];
static uint8_t start_web_client=0;
static uint8_t web_client_attempts=0;
static uint8_t web_client_sendok=0;
static volatile uint8_t sec=0;
static volatile uint8_t cnt2step=0;
static int8_t dns_state=0;
static int8_t gw_arp_state=0;




#define SET_BIT(byte, bit) ((byte) |= (1UL << (bit)))

#define CLEAR_BIT(byte,bit) ((byte) &= ~(1UL << (bit)))

#define IS_SET(byte,bit) (((byte) & (1UL << (bit))) >> (bit))


int GetParameter(char *url, char *parameter, char *output, int outputsize) {
	// check for valid input parameters
	if (!url || !parameter || !output || !outputsize) return 0;
	
	char *occ = strstr(url, parameter);
	if (occ) {
		occ+=strlen(parameter); //skip the parameter itself
		int i =0;
		while(*occ &&  *occ!=' ' && *occ!='&' && i<outputsize) {
			output[i] = *occ;
			i++;
			occ++;
		}
		output[i] = 0;
		return i;
		} else { // given url parameter not found
		output[0] = 0;
		return 0;
	}
}


void ping_callback(uint8_t *ip){
	uint8_t i=0;
	// trigger only first time in case we get many ping in a row:
	if (start_web_client==0){
		start_web_client=1;
		// save IP from where the ping came:
		while(i<4){
			pingsrcip[i]=ip[i];
			i++;
		}
}
}

uint16_t http200ok(void)
{
	return(fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n")));
}
	
ISR(TIMER1_COMPA_vect){
	cnt2step++;
	if (cnt2step>50){
		cnt2step=0;
		sec++; // stepped every second
	}
}

void browserresult_callback(uint16_t webstatuscode,uint16_t datapos, uint16_t len){
	if (webstatuscode==200){
		web_client_sendok++;
	}
}

void arpresolver_result_callback(uint8_t *ip ,uint8_t transaction_number,uint8_t *mac){
	uint8_t i=0;
	if (transaction_number==TRANS_NUM_GWMAC){
		// copy mac address over:
		while(i<6){gwmac[i]=mac[i];i++;}
	}
}

// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage(uint8_t *buf)
{	
	uint8_t data[3];
	uint16_t plen;
	char vstr[17];
	uint8_t err;
	plen=http200ok();
	plen=fill_tcp_data_p(buf,plen,PSTR("<h2>web client status</h2>\n<pre>\n"));
	if (gw_arp_state==1){
		/*if (fetchData(data))
		{
			char chr_temp[3];
			char chr_hum[3];
			plen=fill_tcp_data_p(buf,plen,"<h1 style = 'color: red;'>Teymur_PC</h1></br>");
			plen=fill_tcp_data_p(buf,plen,"Temprature:  ");
			itoa(data[2],chr_temp,10);
			plen=fill_tcp_data_p(buf,plen,chr_temp);
			plen=fill_tcp_data_p(buf,plen,"</br>Humidity:    ");
			itoa(data[0],chr_hum,10);
			plen=fill_tcp_data_p(buf,plen,chr_hum);
		}
		else
		{
			plen=fill_tcp_data_p(buf,plen,"Not working");
		}*/
		plen=fill_tcp_data_p(buf,plen,PSTR("waiting for GW "));
		mk_net_str(vstr,gwip,4,'.',10);
		plen=fill_tcp_data(buf,plen,vstr);
		plen=fill_tcp_data_p(buf,plen,PSTR(" to answer arp.\n"));
		return(plen);
	}
	if (dns_state==1){
		char pg[10];
		plen=fill_tcp_data_p(buf,plen,PSTR("waiting for DNS answer.\n"));
		err=dnslkup_get_error_info();
		plen=fill_tcp_data_p(buf,plen,PSTR("Error code: "));
		itoa(err,vstr,10);
		plen=fill_tcp_data(buf,plen,vstr);
		plen=fill_tcp_data_p(buf,plen,PSTR(" (0=no error)\n"));
		return(plen);
	}
	
	
	plen=fill_tcp_data_p(buf,plen,PSTR("Number of data uploads started by ping: </br>"));
	char web[17];
	//itoa(web_client_sendok, web, 10);
	//plen=fill_tcp_data(buf,plen,web);
	return(plen);
	/*
	// convert number to string:
	itoa(web_client_attempts,vstr,10);
	plen=fill_tcp_data(buf,plen,vstr);
	plen=fill_tcp_data_p(buf,plen,PSTR("\nNumber successful data uploads to web: "));
	// convert number to string:
	itoa(web_client_sendok,vstr,10);
	plen=fill_tcp_data(buf,plen,vstr);
	plen=fill_tcp_data_p(buf,plen,PSTR("\ncheck result: <a href=http://tuxgraphics.org/cgi-bin/upld>http://tuxgraphics.org/cgi-bin/upld</a>"));
	plen=fill_tcp_data_p(buf,plen,PSTR("\n</pre><br><hr>"));
	return(plen);
	*/
}
int main(void)
{
	
	uint8_t data [4];

	uint16_t dat_p,plen;
	char str[20];
	DDRB|= (1<<DDB0);
	DDRB|= (1<<DDB1);
	PORTB|=(1<<PB0);
	_delay_loop_1(0);
	
	enc28j60Init(mymac);
	
	enc28j60clkout(2);
	
	_delay_loop_1(0);
	
	sei();
	
	enc28j60PhyWrite(PHLCON,0x476);
	
	init_udp_or_www_server(mymac,myip);
	www_server_port(MYWWWPORT);
	
	register_ping_rec_callback(&ping_callback);
	
    /* Replace with your application code */
	
    while (1) 
    {
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
				 if (sec > 60){
					 dns_state=0;
				 }
				 // don't try to use web client before
				 // we have a result of dns-lookup
				 continue;
			 
			 //----------
				// /fromdevice/getip.php?pass=7s6d7&di=0F0-000-F0F-000-0F0&ordp=q79ty38jcld5ygbhgkl79l
				 client_browse_url(PSTR("/input/VGvj9xAGQGcRxp7jO75J?private_key=9Yx6pdAY7YtgNXPEpPKo&brewTemp=5353"),NULL,PSTR(WEBSERVER_VHOST),&browserresult_callback,otherside_www_ip,gwmac);
				 
				 
				_delay_loop_1(0);
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
			 dat_p= print_webpage(buf);
			  
			 goto SENDTCP;
		 }
		 //
		 SENDTCP:
		 www_server_reply(buf,dat_p); // send data
		 

	 
	 }
    
}
return (0);
}

