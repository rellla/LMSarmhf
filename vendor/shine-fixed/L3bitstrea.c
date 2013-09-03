/* l3bitstrea.c */

#include "g_includes.h"
#include "Layer3.h"
#include "L3mdct.h"
#include "L3loop.h"
#include "formatbits.h" 
#include "huffman.h"
#include "bitstream.h"
#include "tables.h"
#include "L3bitstrea.h" /* the public interface */

static bitstream_t *bs = NULL;

BF_FrameData    *frameData    = NULL;
BF_FrameResults *frameResults = NULL;

int PartHoldersInitialized = 0;

BF_PartHolder *headerPH;
BF_PartHolder *frameSIPH;
BF_PartHolder *channelSIPH[ MAX_CHANNELS ];
BF_PartHolder *spectrumSIPH[ MAX_GRANULES ][ MAX_CHANNELS ];
BF_PartHolder *scaleFactorsPH[ MAX_GRANULES ][ MAX_CHANNELS ];
BF_PartHolder *codedDataPH[ MAX_GRANULES ][ MAX_CHANNELS ];
BF_PartHolder *userSpectrumPH[ MAX_GRANULES ][ MAX_CHANNELS ];
BF_PartHolder *userFrameDataPH;

static int encodeSideInfo( L3_side_info_t  *si, config_t *config );
static void encodeMainData(int l3_enc[2][2][samp_per_frame2], L3_side_info_t  *si, L3_scalefac_t   *scalefac , config_t *config);
static void write_ancillary_data( char *theData, int lengthInBits );
/*static void drain_into_ancillary_data( int lengthInBits );*/
static void Huffmancodebits( BF_PartHolder **pph, int *ix, gr_info *gi , config_t *config);

/*
 * putMyBits:
 * ----------
 */
void putMyBits( unsigned long int val, unsigned int len, void *config )
{
  putbits( bs, val, len, config );
}

/*
  L3_format_bitstream()
  
  This is called after a frame of audio has been quantized and coded.
  It will write the encoded audio to the bitstream. Note that
  from a layer3 encoder's perspective the bit stream is primarily
  a series of main_data() blocks, with header and side information
  inserted at the proper locations to maintain framing. (See Figure A.7
  in the IS).
*/

void
L3_format_bitstream( int              l3_enc[2][2][samp_per_frame2],
                      L3_side_info_t  *l3_side,
                      L3_scalefac_t   *scalefac,
                      bitstream_t *in_bs,
                      long            (*xr)[2][samp_per_frame2],
                      char             *ancillary,
                      int              ancillary_bits, config_t *config )
{
    int gr, ch, i;
    bs = in_bs;
    
    if ( frameData == NULL )
    {
        frameData = calloc( 1, sizeof(*frameData) );
    }
    if ( frameResults == NULL )
    {
        frameResults = calloc( 1, sizeof(*frameData) );
    }
    if ( !PartHoldersInitialized )
    {
        headerPH = BF_newPartHolder( 12 );
        frameSIPH = BF_newPartHolder( 12 );

        for ( ch = 0; ch < MAX_CHANNELS; ch++ )
            channelSIPH[ch] = BF_newPartHolder( 8 );

        for ( gr = 0; gr < MAX_GRANULES; gr++ ) 
            for ( ch = 0; ch < MAX_CHANNELS; ch++ )
            {
                spectrumSIPH[gr][ch]   = BF_newPartHolder( 32 );
                scaleFactorsPH[gr][ch] = BF_newPartHolder( 64 );
                codedDataPH[gr][ch]    = BF_newPartHolder( samp_per_frame2 );
                userSpectrumPH[gr][ch] = BF_newPartHolder( 4 );
            }
        userFrameDataPH = BF_newPartHolder( 8 );
        PartHoldersInitialized = 1;
    }

    for ( gr = 0; gr < 2; gr++ )
        for ( ch =  0; ch < config->wave.channels; ch++ )
        {
            int *pi = &l3_enc[gr][ch][0];
            long *pr = &xr[gr][ch][0];
            for ( i = 0; i < samp_per_frame2; i++, pr++, pi++ )
            {
                if ( (*pr < 0) && (*pi > 0) )
                    *pi *= -1;
            }
        }

    encodeSideInfo( l3_side, config );
    encodeMainData( l3_enc, l3_side, scalefac , config);
    write_ancillary_data( ancillary, ancillary_bits );

    /*if ( l3_side->resvDrain )*/
        /*drain_into_ancillary_data( l3_side->resvDrain );*/
    /*
      Put frameData together for the call
      to BitstreamFrame()
    */
    frameData->putbits     = &putMyBits;
    frameData->frameLength = config->mpeg.bits_per_frame;
    frameData->nGranules   = 2;
    frameData->nChannels   = config->wave.channels;
    frameData->header      = headerPH->part;
    frameData->frameSI     = frameSIPH->part;

    for ( ch = 0; ch < config->wave.channels; ch++ )
        frameData->channelSI[ch] = channelSIPH[ch]->part;

    for ( gr = 0; gr < 2; gr++ )
        for ( ch = 0; ch < config->wave.channels; ch++ )
        {
            frameData->spectrumSI[gr][ch]   = spectrumSIPH[gr][ch]->part;
            frameData->scaleFactors[gr][ch] = scaleFactorsPH[gr][ch]->part;
            frameData->codedData[gr][ch]    = codedDataPH[gr][ch]->part;
            frameData->userSpectrum[gr][ch] = userSpectrumPH[gr][ch]->part;
        }
    frameData->userFrameData = userFrameDataPH->part;

    BF_BitstreamFrame( frameData, frameResults, (void *)config );

    /* we set this here -- it will be tested in the next loops iteration */
    l3_side->main_data_begin = frameResults->nextBackPtr;
}


static unsigned slen1_tab[16] = { 0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4 };
static unsigned slen2_tab[16] = { 0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3 };

static void encodeMainData(int l3_enc[2][2][samp_per_frame2],
                           L3_side_info_t  *si,
                           L3_scalefac_t   *scalefac, config_t *config )
{
    int gr, ch, sfb;

    for ( gr = 0; gr < 2; gr++ )
                for ( ch = 0; ch < config->wave.channels; ch++ ){
                        scaleFactorsPH[gr][ch]->part->nrEntries = 0;
                    codedDataPH[gr][ch]->part->nrEntries = 0;
                }



        for ( gr = 0; gr < 2; gr++ )
        {
            for ( ch = 0; ch < config->wave.channels; ch++ )
            {
                BF_PartHolder **pph = &scaleFactorsPH[gr][ch];          
                gr_info *gi = &(si->gr[gr].ch[ch].tt);
                unsigned slen1 = slen1_tab[ gi->scalefac_compress ];
                unsigned slen2 = slen2_tab[ gi->scalefac_compress ];
                int *ix = &l3_enc[gr][ch][0];

                {
                    if ( (gr == 0) || (si->scfsi[ch][0] == 0) )
                        for ( sfb = 0; sfb < 6; sfb++ )
                            *pph = BF_addEntry( *pph,  scalefac->l[gr][ch][sfb], slen1 );

                    if ( (gr == 0) || (si->scfsi[ch][1] == 0) )
                        for ( sfb = 6; sfb < 11; sfb++ )
                            *pph = BF_addEntry( *pph,  scalefac->l[gr][ch][sfb], slen1 );

                    if ( (gr == 0) || (si->scfsi[ch][2] == 0) )
                        for ( sfb = 11; sfb < 16; sfb++ )
                            *pph = BF_addEntry( *pph,  scalefac->l[gr][ch][sfb], slen2 );

                    if ( (gr == 0) || (si->scfsi[ch][3] == 0) )
                        for ( sfb = 16; sfb < 21; sfb++ )
                            *pph = BF_addEntry( *pph,  scalefac->l[gr][ch][sfb], slen2 );
                }
                Huffmancodebits( &codedDataPH[gr][ch], ix, gi, config );
            } /* for ch */
        } /* for gr */
} /* main_data */

//static unsigned int crc = 0;

static int encodeSideInfo( L3_side_info_t  *si, config_t *config )
{
    int gr, ch, scfsi_band, region, bits_sent;
    
    headerPH->part->nrEntries = 0;
    headerPH = BF_addEntry( headerPH, 0xfff,                          12 );
    headerPH = BF_addEntry( headerPH, config->mpeg.type,               1 );
/* HEADER HARDCODED SHOULDN`T BE THIS WAY ! */
    headerPH = BF_addEntry( headerPH, 1/*config->mpeg.layr*/,          2 );
    headerPH = BF_addEntry( headerPH, !config->mpeg.crc,               1 );
    headerPH = BF_addEntry( headerPH, config->mpeg.bitrate_index,      4 );
    headerPH = BF_addEntry( headerPH, config->mpeg.samplerate_index,   2 );
    headerPH = BF_addEntry( headerPH, config->mpeg.padding,            1 );
    headerPH = BF_addEntry( headerPH, config->mpeg.ext,                1 );
    headerPH = BF_addEntry( headerPH, config->mpeg.mode,               2 );
    headerPH = BF_addEntry( headerPH, config->mpeg.mode_ext,           2 );
    headerPH = BF_addEntry( headerPH, config->mpeg.copyright,          1 );
    headerPH = BF_addEntry( headerPH, config->mpeg.original,           1 );
    headerPH = BF_addEntry( headerPH, config->mpeg.emph,               2 );
    
    bits_sent = 32;

    frameSIPH->part->nrEntries = 0;

    for (ch = 0; ch < config->wave.channels; ch++ )
        channelSIPH[ch]->part->nrEntries = 0;

    for ( gr = 0; gr < 2; gr++ )
        for ( ch = 0; ch < config->wave.channels; ch++ )
            spectrumSIPH[gr][ch]->part->nrEntries = 0;

        frameSIPH = BF_addEntry( frameSIPH, si->main_data_begin, 9 );

        if ( config->wave.channels == 2 )
            frameSIPH = BF_addEntry( frameSIPH, si->private_bits, 3 );
        else
            frameSIPH = BF_addEntry( frameSIPH, si->private_bits, 5 );
        
        for ( ch = 0; ch < config->wave.channels; ch++ )
            for ( scfsi_band = 0; scfsi_band < 4; scfsi_band++ )
            {
                BF_PartHolder **pph = &channelSIPH[ch];
                *pph = BF_addEntry( *pph, si->scfsi[ch][scfsi_band], 1 );
            }

        for ( gr = 0; gr < 2; gr++ )
            for ( ch = 0; ch < config->wave.channels ; ch++ )
            {
                BF_PartHolder **pph = &spectrumSIPH[gr][ch];
                gr_info *gi = &(si->gr[gr].ch[ch].tt);
                *pph = BF_addEntry( *pph, gi->part2_3_length,        12 );
                *pph = BF_addEntry( *pph, gi->big_values,            9 );
                *pph = BF_addEntry( *pph, gi->global_gain,           8 );
                *pph = BF_addEntry( *pph, gi->scalefac_compress,     4 );
                *pph = BF_addEntry( *pph, 0, 1 );

                    for ( region = 0; region < 3; region++ )
                        *pph = BF_addEntry( *pph, gi->table_select[region], 5 );

                    *pph = BF_addEntry( *pph, gi->region0_count, 4 );
                    *pph = BF_addEntry( *pph, gi->region1_count, 3 );

                *pph = BF_addEntry( *pph, gi->preflag,            1 );
                *pph = BF_addEntry( *pph, gi->scalefac_scale,     1 );
                *pph = BF_addEntry( *pph, gi->count1table_select, 1 );
            }

        if ( config->wave.channels == 2 )
            bits_sent += 256;
        else
            bits_sent += 136;

    return bits_sent;
}

static void write_ancillary_data( char *theData, int lengthInBits )
{
    int bytesToSend = lengthInBits >>3;
    int remainingBits = lengthInBits % 8;
    unsigned wrd;
    int i;

    userFrameDataPH->part->nrEntries = 0;

    for ( i = 0; i < bytesToSend; i++ )
    {
        wrd = theData[i];
        userFrameDataPH = BF_addEntry( userFrameDataPH, wrd, 8 );
    }
    if ( remainingBits )
    {
        /* right-justify remaining bits */
        wrd = theData[bytesToSend] >> (8 - remainingBits);
        userFrameDataPH = BF_addEntry( userFrameDataPH, wrd, remainingBits );
    }
    
}

/*
  Some combinations of bitrate, Fs, and stereo make it impossible to stuff
  out a frame using just main_data, due to the limited number of bits to
  indicate main_data_length. In these situations, we put stuffing bits into
  the ancillary data...
*/
#if ( 0 )
static void
drain_into_ancillary_data( int lengthInBits )
{
    /*
     */
    int wordsToSend   = lengthInBits / 32;
    int remainingBits = lengthInBits % 32;
    int i;

    /*
      userFrameDataPH->part->nrEntries set by call to write_ancillary_data()
    */

    for ( i = 0; i < wordsToSend; i++ )
        userFrameDataPH = BF_addEntry( userFrameDataPH, 0, 32 );
    if ( remainingBits )
        userFrameDataPH = BF_addEntry( userFrameDataPH, 0, remainingBits );    
}
#endif


/*
  Note the discussion of huffmancodebits() on pages 28
  and 29 of the IS, as well as the definitions of the side
  information on pages 26 and 27.
  */
static void
Huffmancodebits( BF_PartHolder **pph, int *ix, gr_info *gi, config_t *config )
{
    int L3_huffman_coder_count1( BF_PartHolder **pph, struct huffcodetab *h, int v, int w, int x, int y );
    int bigv_bitcount( int ix[samp_per_frame2], gr_info *cod_info );

    int region1Start;
    int region2Start;
    int i, bigvalues, count1End;
    int v, w, x, y, bits, cbits, xbits, stuffingBits;
    unsigned int code, ext;
    struct huffcodetab *h;
    int bvbits, c1bits, tablezeros, r0, r1, r2, rt, *pr;
    int bitsWritten = 0;
    //int idx = 0;
    tablezeros = 0;
    r0 = r1 = r2 = 0;
    
    /* 1: Write the bigvalues */
    bigvalues = gi->big_values <<1;
    {
            {
                int *scalefac = &sfBandIndex[config->mpeg.samplerate_index+(config->mpeg.type*3)].l[0];
                unsigned scalefac_index = 100;
                
                    scalefac_index = gi->region0_count + 1;
                    region1Start = scalefac[ scalefac_index ];
                    scalefac_index += gi->region1_count + 1;
                    region2Start = scalefac[ scalefac_index ];

                for ( i = 0; i < bigvalues; i += 2 )
                {
                    unsigned tableindex = 100;
                    /* get table pointer */
                    if ( i < region1Start )
                    {
                        tableindex = gi->table_select[0];
                        pr = &r0;
                    }
                    else
                        if ( i < region2Start )
                        {
                            tableindex = gi->table_select[1];
                            pr = &r1;
                        }
                        else
                        {
                            tableindex = gi->table_select[2];
                            pr = &r2;
                        }
                    h = &ht[ tableindex ];
                    /* get huffman code */
                    x = ix[i];
                    y = ix[i + 1];
                    if ( tableindex )
                    {
                        bits = HuffmanCode( tableindex, x, y, &code, &ext, &cbits, &xbits );
                        *pph = BF_addEntry( *pph,  code, cbits );
                        *pph = BF_addEntry( *pph,  ext, xbits );
                        bitsWritten += rt = bits;
                        *pr += rt;
                    }
                    else
                    {
                        tablezeros += 1;
                        *pr = 0;
                    }
                }
            }
    }
    bvbits = bitsWritten; 

    /* 2: Write count1 area */
    h = &ht[gi->count1table_select + 32];
    count1End = bigvalues + (gi->count1 <<2);
    for ( i = bigvalues; i < count1End; i += 4 )
    {
        v = ix[i];
        w = ix[i+1];
        x = ix[i+2];
        y = ix[i+3];
        bitsWritten += L3_huffman_coder_count1( pph, h, v, w, x, y );
    }
    c1bits = bitsWritten - bvbits;
    if ( (stuffingBits = gi->part2_3_length - gi->part2_length - bitsWritten) )
    {
        int stuffingWords = stuffingBits / 32;
        int remainingBits = stuffingBits % 32;

        /*
          Due to the nature of the Huffman code
          tables, we will pad with ones
        */
        while ( stuffingWords-- )
            *pph = BF_addEntry( *pph, ~0, 32 );
        if ( remainingBits )
            *pph = BF_addEntry( *pph, ~0, remainingBits );
        bitsWritten += stuffingBits;
    }
}

int abs_and_sign( int *x )
{
    if ( *x > 0 ) return 0;
    *x *= -1;
    return 1;
}

int L3_huffman_coder_count1( BF_PartHolder **pph, struct huffcodetab *h, int v, int w, int x, int y )
{
    HUFFBITS huffbits;
    unsigned int signv, signw, signx, signy, p;
    int len;
    int totalBits = 0;
    
    signv = abs_and_sign( &v );
    signw = abs_and_sign( &w );
    signx = abs_and_sign( &x );
    signy = abs_and_sign( &y );
    
    p = v + (w << 1) + (x << 2) + (y << 3);
    huffbits = h->table[p];
    len = h->hlen[ p ];
    *pph = BF_addEntry( *pph,  huffbits, len );
    totalBits += len;
    if ( v )
    {
        *pph = BF_addEntry( *pph,  signv, 1 );
        totalBits += 1;
    }
    if ( w )
    {
        *pph = BF_addEntry( *pph,  signw, 1 );
        totalBits += 1;
    }

    if ( x )
    {
        *pph = BF_addEntry( *pph,  signx, 1 );
        totalBits += 1;
    }
    if ( y )
    {
        *pph = BF_addEntry( *pph,  signy, 1 );
        totalBits += 1;
    }
    return totalBits;
}

/* Implements the pseudocode of page 98 of the IS */
int HuffmanCode(int table_select, int x, int y, unsigned int *code, 
                unsigned int *ext, int *cbits, int *xbits )
{
    unsigned signx, signy, linbitsx, linbitsy, linbits, xlen, ylen, idx;
    struct huffcodetab *h;

    *cbits = 0;
    *xbits = 0;
    *code  = 0;
    *ext   = 0;
    
    if(table_select==0) return 0;
    
    signx = abs_and_sign( &x );
    signy = abs_and_sign( &y );
    h = &(ht[table_select]);
    xlen = h->xlen;
    ylen = h->ylen;
    linbits = h->linbits;
    linbitsx = linbitsy = 0;

    if ( table_select > 15 )
    { /* ESC-table is used */
        if ( x > 14 )
        {
            linbitsx = x - 15;
            x = 15;
        }
        if ( y > 14 )
        {
            linbitsy = y - 15;
            y = 15;
        }

        idx = (x * ylen) + y;
        *code  = h->table[idx];
        *cbits = h->hlen [idx];
        if ( x > 14 )
        {
            *ext   |= linbitsx;
            *xbits += linbits;
        }
        if ( x != 0 )
        {
            *ext <<= 1;
            *ext |= signx;
            *xbits += 1;
        }
        if ( y > 14 )
        {
            *ext <<= linbits;
            *ext |= linbitsy;
            *xbits += linbits;
        }
        if ( y != 0 )
        {
            *ext <<= 1;
            *ext |= signy;
            *xbits += 1;
        }
    }
    else
    { /* No ESC-words */
        idx = (x * ylen) + y;
        *code = h->table[idx];
        *cbits += h->hlen[ idx ];
        if ( x != 0 )
        {
            *code <<= 1;
            *code |= signx;
            *cbits += 1;
        }
        if ( y != 0 )
        {
            *code <<= 1;
            *code |= signy;
            *cbits += 1;
        }
    }
    return *cbits + *xbits;
}

