#include "aes_ni.h"
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <thread>
#include <string>
#include <vector>

#include <assert.h>

int hexdigit_value(char c){
	int nibble = -1;
	if(('0'<=c) && (c<='9')) nibble = c-'0';
	if(('a'<=c) && (c<='f')) nibble = c-'a' + 10;
	if(('A'<=c) && (c<='F')) nibble = c-'A' + 10;
	return nibble;
}

int is_hexdigit(char c){
	return -1!=hexdigit_value(c);
}

size_t hexstr_to_bytes(uint8_t *dst, size_t dst_size, char *hexstr){
	unsigned int len = strlen(hexstr);
	if(dst_size>(len/2))
		dst_size = (len/2);
	memset(dst,0,dst_size);
	for(unsigned int i=0;i<dst_size*2;i++){
		unsigned int shift = 4 - 4*(i & 1);
		unsigned int charIndex = i;//len-1-i;
		char c = hexstr[charIndex];
		uint8_t nibble = hexdigit_value(c);
		dst[i/2] |= nibble << shift;
	}
	return dst_size;
}

void bytes_to_hexstr(char *dst,uint8_t *bytes, unsigned int nBytes){
	unsigned int i;
	for(i=0;i<nBytes;i++){
		sprintf(dst+2*i,"%02X",bytes[i]);
	}
}

size_t cleanup_hexstr(char *hexstr, size_t hexstr_size, char *str, size_t str_size){
	size_t cnt=0;
	int lastIs0=0;
	for(unsigned int j = 0;j<str_size;j++){
		char c = str[j];
		if(is_hexdigit(c)){
			if(cnt==hexstr_size-1){//need final char for null.
				printf("Too many hex digits. hexstr=%s\n",hexstr);
				hexstr[cnt]=0;
				return -1;
			}
			hexstr[cnt++]=c;
		} else if(lastIs0) {
			if('x'==c) cnt--;
			if('X'==c) cnt--;
		}
		lastIs0 = '0'==c;
	}
	hexstr[cnt]=0;
	return cnt;
}

static void print_bytes_sep(const char *msg,const unsigned char *buf, unsigned int size, const char m2[], const char sep[]){
    unsigned int i;
    printf("%s",msg);
    for(i=0;i<size-1;i++) printf("%02X%s",buf[i],sep);
    if(i<size) printf("%02X",buf[i]);
    printf("%s", m2);
}
static void print_128(const char m[], const uint8_t a[16], const char m2[]){
	print_bytes_sep( m,a   ,4,"_","");
	print_bytes_sep("",a+4 ,4,"_","");
	print_bytes_sep("",a+8 ,4,"_","");
	print_bytes_sep("",a+12,4,m2 ,"");
}
static void println_128(const char m[], const uint8_t a[16]){print_128(m,a,"\n");}

size_t user_hexstr_to_bytes(uint8_t*out, size_t out_size, char *str, size_t str_size){
    size_t hexstr_size = cleanup_hexstr(str,str_size,str,str_size);
    size_t conv_size = (hexstr_size/2) < out_size ? hexstr_size/2 : out_size;
	return hexstr_to_bytes(out,conv_size,str);
}

typedef struct u128 {
	uint8_t bytes[16];
} aes128_key_t;

class aes_brute_force{
public:
		static bool done;

		static void reset(){
			done=false;
		}
		static bool is_done(){
			return done;
		}
		static void set_done(){
			done=true;
		}
    static unsigned int mask_to_offsets(uint8_t key_mask[16], unsigned int offsets[16]){
			unsigned int n_offsets = 0;
			unsigned int nbits = 0;
			for(unsigned int i=0;i<16;i++){
					if(key_mask[i]){//byte granularity
							offsets[n_offsets++] = i;
							nbits+=8;
					}
			}
			return n_offsets;
		}

		static void search(	unsigned int offsets[16],
												unsigned int n_offsets,
												uint8_t key[16],					//I/O
												uint8_t plain[16],
												uint8_t cipher[16],
												uint64_t &loop_cnt,				//output the number of iteration actually done
												bool &found								//output
											){
			uint8_t r[16];
			unsigned int nbits = n_offsets*8;
			uint64_t n_loops = 1;
			n_loops = n_loops << nbits;
			found=false;
			for(loop_cnt=0;loop_cnt<n_loops;loop_cnt++){
					uint64_t cnt=loop_cnt;
					__m128i key_schedule[11];
					for(unsigned int o=0;o<n_offsets;o++){
							key[offsets[o]] = (uint8_t)cnt;
							cnt = cnt >> 8;
					}
					aes128_load_key_enc_only(key,key_schedule);
					aes128_enc(key_schedule,plain,r);

					if(0==memcmp(r,cipher,16)){
							found=true;
							done=true;
							return;
					}
			}
		}

		explicit aes_brute_force(uint8_t key_mask[16],uint8_t key[16],uint8_t plain[16],uint8_t cipher[16]){
			aes128_key_t k;

			memcpy(&k            ,key     ,16);
			memcpy(this->plain   ,plain   ,16);
			memcpy(this->cipher  ,cipher  ,16);

			keys.push_back(k);
			n_offsets = mask_to_offsets(key_mask, offsets);
			nbits = n_offsets*8;
		}
		void compute() {
			loop_cnt=0;
			for(auto k=keys.begin();k!=keys.end();++k){
				uint64_t cnt;
				search(offsets, n_offsets, k->bytes, plain, cipher,cnt,found);
				loop_cnt+=cnt;
				if(found){
					memcpy(correct_key,k->bytes,16);
					return;
				}
				if(is_done()){ //used for multithread operations
					return;
				}
			}
    }
		void operator()() {
      compute();
    }
		void push(uint8_t key[16]){
			aes128_key_t k;
			memcpy(&k            ,key     ,16);
			keys.push_back(k);
		}
		uint64_t loop_cnt;
		bool found;
		unsigned int offsets[16];
		unsigned int n_offsets;
		unsigned int nbits;
		std::vector<aes128_key_t> keys;
		uint8_t correct_key[16];
		uint8_t plain[16];
		uint8_t cipher[16];
};
bool aes_brute_force::done=false;


int main (int argc, char*argv[]){
    uint8_t key_mask[16]={0};
    uint8_t key_in[16]  ={0};
    uint8_t plain[16]   ={0};
    uint8_t cipher[16]  ={0};

		const char *key_mask_str = "FF0000FF_00FF0000_0000FF00_00000000";
    const char *key_in_str   = "007E1500_2800D2A6_ABF70088_09CF4F3C";
		//                          2B7E1516_28AED2A6_ABF71588_09CF4F3C
    const char *plain_str    = "3243F6A8_885A308D_313198A2_E0370734";
    const char *cipher_str   = "3925841D_02DC09FB_DC118597_196A0B32";
    char buf[4][1024]={0};
    memcpy(buf[0],key_mask_str,strlen(key_mask_str));
    memcpy(buf[1],key_in_str  ,strlen(key_in_str));
    memcpy(buf[2],plain_str   ,strlen(plain_str ));
    memcpy(buf[3],cipher_str  ,strlen(cipher_str));
    char *demo_argv[] = {argv[0],buf[0],buf[1],buf[2],buf[3]};

    if(0!=aes128_self_test()){
        std::cerr << "ERROR: AES-NI self test failed" << std::endl;
        exit(-1);
    }
		bool test_demo = false;
    if( (argc<2) || ((argc>1) && (strlen(argv[1])<16)) ){
        std::cerr << "AES128 encryption key brute force search" << std::endl;
        std::cerr << "Usage: " << argv[0] << " <key_mask> <key_in> <plain> <cipher> [n_threads]" << std::endl;
        std::cerr << std::endl;
				std::cerr << "launching test/demo..." << std::endl << std::endl;
				argc = 5;
        argv=demo_argv;
				for(int i=0;i<argc;i++){
						std::cerr << argv[i] << " ";
				}
				std::cerr << std::endl << std::endl;
        test_demo=true;
    }

    unsigned int len;
    len=user_hexstr_to_bytes(key_mask,16,argv[1],strlen(argv[1])+1);assert(16==len);
    len=user_hexstr_to_bytes(key_in  ,16,argv[2],strlen(argv[2])+1);assert(16==len);
    len=user_hexstr_to_bytes(plain   ,16,argv[3],strlen(argv[3])+1);assert(16==len);
    len=user_hexstr_to_bytes(cipher  ,16,argv[4],strlen(argv[4])+1);assert(16==len);


		unsigned int n_threads = std::thread::hardware_concurrency();
		if(0==n_threads) n_threads=1;
		if(argc>5){
			n_threads= std::stoi(argv[5],0,0);
		}

		std::cout << "INFO: " << n_threads << " concurrent threads supported in hardware." << std::endl << std::endl;
		std::cout << "Search parameters:" << std::endl;
		std::cout <<"\tn_threads:    " << n_threads << std::endl;
		println_128("\tkey_mask:     ",key_mask);
    println_128("\tkey_in:       ",key_in);
    println_128("\tplain:        ",plain);
    println_128("\tcipher:       ",cipher);
		std::cout << std::endl;

		unsigned int offsets[16];
		unsigned int n_offsets = aes_brute_force::mask_to_offsets(key_mask, offsets);
		uint8_t jobs_key_mask[16];
		memcpy(jobs_key_mask,key_mask,16);
		if(1==n_offsets){
			n_threads = 1;
			std::cout << "INFO: n_threads set to 1 because n_offsets=1"<< std::endl;
		}
		if(1!=n_threads) {
			jobs_key_mask[offsets[0]] = 0;//fix this key byte at the job level.
		}
		println_128("\tjobs_key_mask:",jobs_key_mask);
		std::vector<std::thread *> threads(n_threads);
		std::vector<aes_brute_force *> jobs(n_threads);
		aes_brute_force::reset();

		int n_keys_per_thread = n_threads==1 ? 0 : (256+n_threads-1) / n_threads;
		unsigned int jobs_cnt=0;
		for(unsigned int thread_i=0;thread_i<n_threads;thread_i++){
			key_in[offsets[0]]=jobs_cnt++;
			jobs.at(thread_i) = new aes_brute_force(jobs_key_mask, key_in, plain, cipher);
			for(int i=0;i<n_keys_per_thread-1;i++){
				key_in[offsets[0]]=jobs_cnt++;
				jobs.at(thread_i)->push(key_in);
			}
		}
		std::cout  << std::endl << "Launching " << n_offsets*8<< " bits search" << std::endl;
		if(test_demo) std::cout << "This can take a couple of minutes on slow computers." << std::endl;
		std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
		for(unsigned int thread_i=0;thread_i<n_threads;thread_i++){
			threads.at(thread_i)=new std::thread(&aes_brute_force::compute, jobs.at(thread_i));
		}

		bool found = false;
		uint64_t loop_cnt=0;
		int winner=-1;
		for(unsigned int thread_i=0;thread_i<n_threads;thread_i++){
			threads.at(thread_i)->join();
			if(jobs.at(thread_i)->found){
				found=true;
				winner = thread_i;
				memcpy(key_in,jobs.at(thread_i)->correct_key,16);

			}
			loop_cnt+=jobs.at(thread_i)->loop_cnt;
		}
		std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
		std::cout << std::endl;
		if(found){
				std::cout << "Thread " << winner << " claims to have found the key" << std::endl;
        println_128("\tkey found:    ",key_in);
    } else {
        std::cout << "No matching key could be found" << std::endl;
    }
		std::cout << std::endl << "Performances:" << std::endl;
    std::cout << "\t" << std::dec << loop_cnt << " AES128 operations done in ";
    std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);
    std::cout << time_span.count() << "s" << std::endl;
    uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t2-t1).count();
    unsigned int aes_op_duration_ns = ns / loop_cnt;
    std::cout << "\t" << aes_op_duration_ns << "ns per AES128 operation" <<std::endl;
    uint64_t key_per_sec = loop_cnt / time_span.count();
		if(key_per_sec>1000000){
    	std::cout << "\t" << std::fixed << std::setprecision(2) << key_per_sec/1000000.0 << " million keys per second" << std::endl;
		}else{
			std::cout << "\t" << key_per_sec << " keys per second" << std::endl;
		}
		if(test_demo){
			if(found){
				const uint8_t expected_key[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
				if(memcmp(expected_key,key_in,16)){
					std::cout << "ERROR: key found is not the expected one! That worth a debug session!" << std::endl;
					return -1;
				} else {
					std::cout << "INFO: found the expected key, test passed." << std::endl;
				}
			} else {
				std::cout << "ERROR: key could not be found! That worth a debug session!" << std::endl;
				return -1;
			}
		}

    return 0;
}
