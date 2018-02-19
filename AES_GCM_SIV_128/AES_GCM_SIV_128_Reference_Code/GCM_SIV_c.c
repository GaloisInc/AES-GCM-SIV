/*
###############################################################################
# AES-GCM-SIV developers and authors:                                         #
#                                                                             #
# Shay Gueron,    University of Haifa, Israel and                             #
#                 Intel Corporation, Israel Development Center, Haifa, Israel #
# Adam Langley,   Google                                                      #
# Yehuda Lindell, Bar Ilan University                                         #
###############################################################################
#                                                                             #
# References:                                                                 #
#                                                                             #
# [1] S. Gueron, Y. Lindell, GCM-SIV: Full Nonce Misuse-Resistant             #
# Authenticated Encryption at Under One Cycle per Byte,                       #
# 22nd ACM Conference on Computer and Communications Security,                #
# 22nd ACM CCS: pages 109-119, 2015.                                          #
# [2] S. Gueron, A. Langley, Y. Lindell, AES-GCM-SIV: Nonce Misuse-Resistant  #
# Authenticated Encryption.                                                   #
# https://tools.ietf.org/html/draft-gueron-gcmsiv-02#                         #
###############################################################################
#                                                                             #
###############################################################################
#                                                                             #
# Copyright (c) 2016, Shay Gueron                                             #
#                                                                             #
#                                                                             #
# Permission to use this code for AES-GCM-SIV is granted.                     #
#                                                                             #
# Redistribution and use in source and binary forms, with or without          #
# modification, are permitted provided that the following conditions are      #
# met:                                                                        #
#                                                                             #
# * Redistributions of source code must retain the above copyright notice,    #
#   this list of conditions and the following disclaimer.                     #
#                                                                             #
# * Redistributions in binary form must reproduce the above copyright         #
#   notice, this list of conditions and the following disclaimer in the       #
#   documentation and/or other materials provided with the distribution.      #
#                                                                             #
# * The names of the contributors may not be used to endorse or promote       #
# products derived from this software without specific prior written          #
# permission.                                                                 #
#                                                                             #
###############################################################################
#                                                                             #
###############################################################################
# THIS SOFTWARE IS PROVIDED BY THE AUTHORS ""AS IS"" AND ANY                  #
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE           #
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR          #
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL CORPORATION OR              #
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,       #
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,         #
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR          #
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF      #
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING        #
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS          #
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                #
###############################################################################
*/
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "aes_emulation.h"
#include "clmul_emulator.h"

#if !defined (ALIGN16)
#if defined (__GNUC__)
#  define ALIGN16  __attribute__  ( (aligned (16)))
# else
#  define ALIGN16 __declspec (align (16))
# endif
#endif

#define XOR_WITH_NONCE

static const uint32_t rcon[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};

void AES_128_Key_Expansion(const unsigned char *userkey, uint32_t* ks)
{   
    const unsigned char *key =  userkey;
    uint32_t* w = ks;
    const int Nk = 4;
    const int Nb = 4;
    const int Nr = 10;

    int i = 0;
    while (i < Nk)
    {
        w[i] = key[4*i] ^ (key[4*i+1]<<8) ^ (key[4*i+2]<<16) ^ (key[4*i+3]<<24);
        i++;
    }
    i = Nk;
    while (i < Nb * (Nr + 1))
    {
        uint32_t temp = w[i - 1];
        if(i%Nk == 0)
        {
            temp = 
                (emulated_aesenc_rijndael_sbox[(temp      ) & 0xff] << 24) ^
				(emulated_aesenc_rijndael_sbox[(temp >>  8) & 0xff] << 0) ^
				(emulated_aesenc_rijndael_sbox[(temp >> 16) & 0xff] << 8) ^
				(emulated_aesenc_rijndael_sbox[(temp >> 24)       ] << 16) ^
				rcon[i/Nk - 1];
        }
        w[i] = w[i - Nk] ^ temp;
        i++;
    }
}

void AES_128_Encrypt(uint32_t* out, uint32_t* in, uint32_t* ks)
{
    int i, j;

    out[0] = in[0]^ks[0];
    out[1] = in[1]^ks[1];
    out[2] = in[2]^ks[2];
    out[3] = in[3]^ks[3];
    ks+=4;

    for(i=0; i<9; i++)
    {
        emulated_aesenc(out,ks);
        ks+=4;
    }
    emulated_aesenclast(out,ks);
}

void AES_128_CTR(uint8_t* out, uint8_t* in, uint32_t* CTR, int mlen, uint32_t* ks)
{
    uint32_t EK[4];
    uint32_t *P = (uint32_t*)in;
    uint32_t *C = (uint32_t*)out;
    int i;
    for(i=0; i<mlen/16; i++)
    {
        AES_128_Encrypt(EK, CTR, ks);
        C[0] = EK[0] ^ P[0];
        C[1] = EK[1] ^ P[1];
        C[2] = EK[2] ^ P[2];
        C[3] = EK[3] ^ P[3];
        P+=4;
        C+=4;
        // CTR[3] = bswap_32(bswap_32(CTR[3]) + 1);
		CTR[0] = CTR[0] + 1; // ((CTR[0] +1) % (0xFFFFFFFF));
    }
    if(i*16 < mlen)
    {
        AES_128_Encrypt(EK, CTR, ks);
        // CTR[3] = bswap_32(bswap_32(CTR[3]) + 1);
        CTR[0] = CTR[0] +1;
		for(i*=16; i<mlen; i++)
        {
            out[i] = ((uint8_t*)EK)[i%16] ^ in[i];
        }
    }
}


void gfmul_int(uint64_t* a, uint64_t* b, uint64_t* res){  
    uint64_t tmp1[2], tmp2[2], tmp3[2], tmp4[2];
	uint64_t XMMMASK[2] = {0x1, 0xc200000000000000};

    vclmul_emulator(a,b,tmp1,0x00);
    vclmul_emulator(a,b,tmp3,0x10);
    vclmul_emulator(a,b,tmp2,0x01);
    vclmul_emulator(a,b,tmp4,0x11);
	
	tmp2[0] ^= tmp3[0];
	tmp2[1] ^= tmp3[1];

	tmp3[0] = 0;
	tmp3[1] = tmp2[0];
    
	tmp2[0] = tmp2[1];
	tmp2[1] = 0;
	
	tmp1[0] ^= tmp3[0];
	tmp1[1] ^= tmp3[1];
	
	tmp4[0] ^= tmp2[0];
	tmp4[1] ^= tmp2[1];
	
    vclmul_emulator(XMMMASK,tmp1,tmp2,0x01);
	((uint32_t*)tmp3)[0] = ((uint32_t*)tmp1)[2];
	((uint32_t*)tmp3)[1] = ((uint32_t*)tmp1)[3];
	((uint32_t*)tmp3)[2] = ((uint32_t*)tmp1)[0];
	((uint32_t*)tmp3)[3] = ((uint32_t*)tmp1)[1];
	
	tmp1[0] = tmp2[0] ^ tmp3[0];
	tmp1[1] = tmp2[1] ^ tmp3[1];

    vclmul_emulator(XMMMASK,tmp1,tmp2,0x01);
    ((uint32_t*)tmp3)[0] = ((uint32_t*)tmp1)[2];
	((uint32_t*)tmp3)[1] = ((uint32_t*)tmp1)[3];
	((uint32_t*)tmp3)[2] = ((uint32_t*)tmp1)[0];
	((uint32_t*)tmp3)[3] = ((uint32_t*)tmp1)[1];
	
	tmp1[0] = tmp2[0] ^ tmp3[0];
	tmp1[1] = tmp2[1] ^ tmp3[1];
	
	res[0] = tmp4[0] ^ tmp1[0];
	res[1] = tmp4[1] ^ tmp1[1];
}

void POLYVAL(uint64_t* input, uint64_t* H, uint64_t len, uint64_t* result)
{	
    
	ALIGN16 
	uint64_t current_res[2];
	uint64_t in[2];
	current_res[0] = result[0];
    current_res[1] = result[1];

	int i;
	int blocks = len/16;
	if (blocks == 0) return;
	
	for (i = 0; i < blocks; i++) {
		//XOR with buffer
		in[0] = input[2*i];
        in[1] = input[2*i+1];
		
		current_res[0] ^= in[0];
		current_res[1] ^= in[1];
		gfmul_int(current_res, H, current_res);
	}
	result[0] = current_res[0];
	result[1] = current_res[1];
}


void GCM_SIV_ENC_2_Keys(uint8_t* CT, uint8_t TAG[16], uint8_t K1[16], uint8_t N[12], uint8_t* AAD, uint8_t* MSG, 
						uint64_t AAD_len, uint64_t MSG_len)
{
	uint64_t POLYV[2] = {0};
	uint64_t CTR[2] = {0};
	uint64_t KS[24];
	uint8_t ENC_KEY[16] = {0};
	uint8_t HASH_KEY[16] = {0};
	uint64_t msg_pad = 0;
	uint64_t aad_pad = 0;
	uint64_t T[8] = {0};
	uint32_t _N[4] = {0};
	int i;
	uint64_t LENBLK[2] = {(AAD_len<<3), (MSG_len<<3)};

	if ((AAD_len % 16) != 0) {
		aad_pad = 16 - (AAD_len % 16);
	}
	if ((MSG_len % 16) != 0) {
		msg_pad = 16 - (MSG_len % 16);
	}
	AES_128_Key_Expansion(K1, (uint32_t*)KS);
	_N[1] = ((uint32_t*)N)[0];
	_N[2] = ((uint32_t*)N)[1];
	_N[3] = ((uint32_t*)N)[2];
	for (i=0; i<4; i++)
	{
		_N[0] = i ;
		AES_128_Encrypt((((uint32_t*)T)+4*i), (uint32_t*)_N, (uint32_t*)KS);
	}
	((uint64_t*)HASH_KEY)[0] = T[0];
	((uint64_t*)HASH_KEY)[1] = T[2];
	((uint64_t*)ENC_KEY)[0] = T[4];
	((uint64_t*)ENC_KEY)[1] = T[6];
//	AES_128_Encrypt((uint32_t*)HASH_KEY, (uint32_t*)N, (uint32_t*)KS);
//	AES_128_Encrypt((uint32_t*)ENC_KEY, (uint32_t*)HASH_KEY, (uint32_t*)KS);
	AES_128_Key_Expansion(ENC_KEY, (uint32_t*)KS);
	POLYVAL((uint64_t*)AAD, (uint64_t*)HASH_KEY, AAD_len + aad_pad, POLYV);
	POLYVAL((uint64_t*)MSG, (uint64_t*)HASH_KEY, MSG_len + msg_pad, POLYV);
	POLYVAL(LENBLK, (uint64_t*)HASH_KEY, 16, POLYV);
	#ifdef XOR_WITH_NONCE
	((uint64_t*)POLYV)[0] ^= ((uint64_t*)N)[0];
	((uint64_t*)POLYV)[1] ^= ((uint32_t*)N)[2];
	#endif
	((uint8_t*)POLYV)[15] &= 127;
	
	AES_128_Encrypt((uint32_t*)TAG, (uint32_t*)POLYV, (uint32_t*)KS); 
	
	CTR[0] = ((uint64_t*)TAG)[0];
	CTR[1] = ((uint64_t*)TAG)[1];
	((uint8_t*)CTR)[15] |= 128;

	
#ifdef DETAILS
	printf("\nRecord_Hash_Key =               "); print16((uint8_t*)HASH_KEY);
	printf("\nRecord_Enc_Key =                "); print16((uint8_t*)ENC_KEY);
	printf("\nLENBLK =                        "); print16((uint8_t*)LENBLK);
	printf("\nPOLYVAL xor N (Msbit cleared) = "); print16((uint8_t*)POLYV);
	printf("\nTAG =                           "); print16(TAG);
	printf("\nCTRBLK =                        "); print16((uint8_t*)CTR);
#endif
	
	AES_128_CTR(CT, MSG, (uint32_t*)CTR, MSG_len + msg_pad, (uint32_t*)KS);
}


int GCM_SIV_DEC_2_Keys(uint8_t* MSG, uint8_t TAG[16], uint8_t K1[16], uint8_t N[12], uint8_t* AAD, uint8_t* CT, 
						uint64_t AAD_len, uint64_t MSG_len)
{
	uint64_t T[2] = {0};
	uint64_t new_TAG[2] = {0};
	uint64_t CTR[2] = {0};
	uint64_t KS[24];
	uint64_t msg_pad = 0;
	uint64_t aad_pad = 0;
	uint8_t KEY_ENC[16] = {0};
	uint8_t HASH_KEY[16] = {0};
	uint64_t _T[8] = {0};
	uint32_t _N[4] = {0};
	int i;
	uint64_t LENBLK[2] = {(AAD_len<<3), (MSG_len<<3)};
	if ((AAD_len % 16) != 0) {
		aad_pad = 16 - (AAD_len % 16);
	}
	if ((MSG_len % 16) != 0) {
		msg_pad = 16 - (MSG_len % 16);
	}
	
	CTR[0] = ((uint64_t*)TAG)[0];
	CTR[1] = ((uint64_t*)TAG)[1];
	((uint8_t*)CTR)[15] |= 128;
	
	AES_128_Key_Expansion(K1, (uint32_t*)KS);
	_N[1] = ((uint32_t*)N)[0];
	_N[2] = ((uint32_t*)N)[1];
	_N[3] = ((uint32_t*)N)[2];
	for (i=0; i<4; i++)
	{
		_N[0] = i ;
		AES_128_Encrypt((((uint32_t*)_T)+4*i), (uint32_t*)_N, (uint32_t*)KS);
	}
	((uint64_t*)HASH_KEY)[0] = _T[0];
	((uint64_t*)HASH_KEY)[1] = _T[2];
	((uint64_t*)KEY_ENC)[0] = _T[4];
	((uint64_t*)KEY_ENC)[1] = _T[6];
	//AES_128_Encrypt((uint32_t*)HASH_KEY, (uint32_t*)N, (uint32_t*)KS);
	//AES_128_Encrypt((uint32_t*)KEY_ENC, (uint32_t*)HASH_KEY, (uint32_t*)KS);
	#ifdef DETAILS
	printf("\nEncryption_Key =                "); print16(KEY_ENC);
    #endif
	AES_128_Key_Expansion(KEY_ENC, (uint32_t*)KS);
	AES_128_CTR(MSG, CT, (uint32_t*)CTR, MSG_len + msg_pad, (uint32_t*)KS);
	
	POLYVAL((uint64_t*)AAD, (uint64_t*)HASH_KEY, AAD_len + aad_pad, T);
	POLYVAL((uint64_t*)MSG, (uint64_t*)HASH_KEY, MSG_len + msg_pad, T);
	POLYVAL(LENBLK, (uint64_t*)HASH_KEY, 16, T);
	
	new_TAG[0] = T[0];
	new_TAG[1] = T[1];
	#ifdef XOR_WITH_NONCE
	((uint64_t*)new_TAG)[0] ^= ((uint64_t*)N)[0];
	((uint64_t*)new_TAG)[1] ^= ((uint32_t*)N)[2];
	#endif
	((uint8_t*)new_TAG)[15] &= 127;
	AES_128_Encrypt((uint32_t*)new_TAG, (uint32_t*)new_TAG, (uint32_t*)KS); 

#ifdef DETAILS
	printf("\nTAG' =                          "); print16(new_TAG);
#endif

	if ( (new_TAG[0] == ((uint64_t*)TAG)[0]) && (new_TAG[1] == ((uint64_t*)TAG)[1]) ) {
		return 1;
	}
	// upon tag mismatch, the output is a copy of the input ciphertext (and a mismatch indicator)
	for (i=0; i<(MSG_len + msg_pad); i++)
	{
		MSG[i] = CT[i];
	}
	return 0;
}




						
