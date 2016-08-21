/*
** Copyright (C) 2013-2016 Erik de Castro Lopo <erikd@mega-nerd.com>
**
** This program is free software ; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation ; either version 2.1 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY ; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
** GNU Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this program ; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/


#include "sfconfig.h"

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "sndfile.h"
#include "sfendian.h"
#include "common.h"

#if (ENABLE_EXPERIMENTAL_CODE && HAVE_EXTERNAL_XIPH_LIBS)

#include <ogg/ogg.h>
#include <opus/opus.h>
#include <opus/opusfile.h>

#include "ogg.h"

typedef struct
{	int32_t serialno ;

	OpusDecoder * dec ;
	OpusHead head ;
	OpusTags tags ;
	void * state ;
} OPUS_PRIVATE ;

typedef int convert_func (SF_PRIVATE *psf, int, void *, int, int, float **) ;

static int	ogg_opus_read_header (SF_PRIVATE * psf, int log_data) ;
static int	ogg_opus_close (SF_PRIVATE *psf) ;
static int	ogg_opus_byterate (SF_PRIVATE *psf) ;
static sf_count_t	ogg_opus_length (SF_PRIVATE *psf) ;
//static sf_count_t	ogg_opus_read_s (SF_PRIVATE *psf, short *ptr, sf_count_t len) ;
//static sf_count_t	ogg_opus_read_i (SF_PRIVATE *psf, int *ptr, sf_count_t len) ;
//static sf_count_t	ogg_opus_read_f (SF_PRIVATE *psf, float *ptr, sf_count_t len) ;
//static sf_count_t	ogg_opus_read_sample (SF_PRIVATE *psf, void *ptr, sf_count_t lens, convert_func *transfn) ;

typedef struct
{	int id ;
	const char *name ;
} STR_PAIRS ;

static STR_PAIRS ogg_opus_metatypes [] =
{	{	SF_STR_TITLE,		"Title" },
	{	SF_STR_COPYRIGHT,	"Copyright" },
	{	SF_STR_SOFTWARE,	"Software" },
	{	SF_STR_ARTIST,		"Artist" },
	{	SF_STR_COMMENT,		"Comment" },
	{	SF_STR_DATE,		"Date" },
	{	SF_STR_ALBUM,		"Album" },
	{	SF_STR_LICENSE,		"License" },
	{	SF_STR_TRACKNUMBER,	"Tracknumber" },
	{	SF_STR_GENRE, 		"Genre" },
} ;

int
ogg_opus_open (SF_PRIVATE *psf)
{	OGG_PRIVATE* odata = psf->container_data ;
	OPUS_PRIVATE* oopus = calloc (1, sizeof (OPUS_PRIVATE)) ;
	int	error = 0 ;

	if (odata == NULL)
	{	psf_log_printf (psf, "%s : odata is NULL???\n", __func__) ;
		return SFE_INTERNAL ;
		} ;

	psf->codec_data = oopus ;
	if (oopus == NULL)
		return SFE_MALLOC_FAILED ;

	if (psf->file.mode == SFM_RDWR)
		return SFE_BAD_MODE_RW ;

	if (psf->file.mode == SFM_READ)
	{	/* Call this here so it only gets called once, so no memory is leaked. */
		ogg_sync_init (&odata->osync) ;

		if ((error = ogg_opus_read_header (psf, 1)))
			return error ;

#if 0
		psf->read_short		= ogg_opus_read_s ;
		psf->read_int		= ogg_opus_read_i ;
		psf->read_float		= ogg_opus_read_f ;
		psf->read_double	= ogg_opus_read_d ;
#endif
		psf->sf.frames		= ogg_opus_length (psf) ;
		} ;

	psf->codec_close = ogg_opus_close ;

	if (psf->file.mode == SFM_WRITE)
	{
#if 0
		/* Set the default oopus quality here. */
		vdata->quality = 0.4 ;

		psf->write_header	= ogg_opus_write_header ;
		psf->write_short	= ogg_opus_write_s ;
		psf->write_int		= ogg_opus_write_i ;
		psf->write_float	= ogg_opus_write_f ;
		psf->write_double	= ogg_opus_write_d ;
#endif

		psf->sf.frames = SF_COUNT_MAX ; /* Unknown really */
		psf->strings.flags = SF_STR_ALLOW_START ;
		} ;

	psf->bytewidth = 1 ;
	psf->blockwidth = psf->bytewidth * psf->sf.channels ;

#if 0
	psf->seek = ogg_opus_seek ;
	psf->command = ogg_opus_command ;
#endif
	psf->byterate = ogg_opus_byterate ;

	/* FIXME, FIXME, FIXME : Hack these here for now and correct later. */
	psf->sf.format = SF_FORMAT_OGG | SF_FORMAT_OPUS ;
	psf->sf.sections = 1 ;

	psf->datalength = 1 ;
	psf->dataoffset = 0 ;
	/* End FIXME. */

	return error ;
} /* ogg_opus_open */


static int
ogg_opus_get_next_page (SF_PRIVATE *psf, ogg_sync_state * osync, ogg_page *page)
{	static const int CHUNK_SIZE = 4500 ;

	while (ogg_sync_pageout (osync, page) <= 0)
	{	char * buffer = ogg_sync_buffer (osync, CHUNK_SIZE) ;
		int bytes = psf_fread (buffer, 1, 4096, psf) ;

		if (bytes <= 0)
		{	ogg_sync_wrote (osync, 0) ;
			return 0 ;
			} ;

		ogg_sync_wrote (osync, bytes) ;
		} ;

	return 1 ;
} /* ogg_opus_get_next_page */

static int
ogg_opus_read_header (SF_PRIVATE * psf, int log_data)
{
	OGG_PRIVATE *odata = (OGG_PRIVATE *) psf->container_data ;
	OPUS_PRIVATE *opdata = (OPUS_PRIVATE *) psf->codec_data ;
	char *buffer ;
	int	bytes ;
	int i, nn ;

	odata->eos = 0 ;

	/* Weird stuff happens if these aren't called. */
	ogg_stream_reset (&odata->ostream) ;
	ogg_sync_reset (&odata->osync) ;

	/*
	**	Grab some data at the head of the stream.  We want the first page
	**	(which is guaranteed to be small and only contain the Vorbis
	**	stream initial header) We need the first page to get the stream
	**	serialno.
	*/

	/* Expose the buffer */
	buffer = ogg_sync_buffer (&odata->osync, 4096L) ;

	/* Grab the part of the header that has already been read. */
	memcpy (buffer, psf->header, psf->headindex) ;
	bytes = psf->headindex ;

	/* Submit a 4k block to  Ogg layer */
	bytes += psf_fread (buffer + psf->headindex, 1, 4096 - psf->headindex, psf) ;
	ogg_sync_wrote (&odata->osync, bytes) ;

	/* Get the first page. */
	if ((nn = ogg_sync_pageout (&odata->osync, &odata->opage)) != 1)
	{
		/* Have we simply run out of data?  If so, we're done. */
		if (bytes < 4096)
			return 0 ;

		/* Error case.  Must not be Opus data */
		psf_log_printf (psf, "Input does not appear to be an Ogg bitstream.\n") ;
		return SFE_MALFORMED_FILE ;
		} ;

	/*
	**	Get the serial number and set up the rest of decode.
	**	Serialno first ; use it to set up a logical stream.
	*/
	ogg_stream_clear (&odata->ostream) ;
	ogg_stream_init (&odata->ostream, ogg_page_serialno (&odata->opage)) ;

	if (ogg_stream_pagein (&odata->ostream, &odata->opage) < 0)
	{	/* Error ; stream version mismatch perhaps. */
		psf_log_printf (psf, "Error reading first page of Ogg bitstream data\n") ;
		return SFE_MALFORMED_FILE ;
		} ;

	if (ogg_stream_packetout (&odata->ostream, &odata->opacket) != 1)
	{	/* No page? must not be opus. */
		psf_log_printf (psf, "Error reading initial header packet.\n") ;
		return SFE_MALFORMED_FILE ;
		} ;

	if (opus_head_parse (&opdata->head, odata->opacket.packet, odata->opacket.bytes) < 0)
	{	/* Error */
		psf_log_printf (psf, "Failed to read OpusHead header.\n") ;
		return SFE_MALFORMED_FILE ;
	}

	/*
	**	At this point, we're sure we're Opus. We've set up the logical (Ogg)
	**	bitstream decoder. Get the OpusTags header and set up the Opus decoder
	**	The OpusTags header can be large and span multiple pages, Thus we read
	**	and submit data until we get the packet, watching that no pages are 
	**	missing.  If a page is missing, error out ; losing a header page is
	**	the only place where missing data is fatal.
	*/

	opus_tags_init (&opdata->tags) ;

	i = 0 ;			/* Count of number of packets read */
	while (i < 1) {
		int result = ogg_opus_get_next_page (psf, &odata->osync, &odata->opage) ;
		if (result == 0)
		{
			psf_log_printf (psf, "End of file before finding all Opus headers!\n") ;
			return SFE_MALFORMED_FILE ;
		}
		else if (result == 1)
		{	/*
			**	Don't complain about missing or corrupt data yet. We'll
			**	catch it at the packet output phase.
			**
			**	We can ignore any errors here as they'll also become apparent
			**	at packetout.
			*/
			nn = ogg_stream_pagein (&odata->ostream, &odata->opage) ;
			result = ogg_stream_packetout (&odata->ostream, &odata->opacket) ;
			if (result == 0)
				continue ;
			if (result < 0)
			{	/*	Uh oh ; data at some point was corrupted or missing!
				**	We can't tolerate that in a header. Die. */
				psf_log_printf (psf, "Corrupt secondary header.	Exiting.\n") ;
				return SFE_MALFORMED_FILE ;
			} ;

			if (opus_tags_parse (&opdata->tags, odata->opacket.packet, odata->opacket.bytes) < 0)
			{
				psf_log_printf (psf, "Encountered invalid OpusTags\n") ;
				return SFE_MALFORMED_FILE ;
			}
			i++ ;
		} ;
	} ;

	if (log_data)
	{	int printed_metadata_msg = 0 ;
		int k ;

		psf_log_printf (psf, "Bitstream is %d channel, %D Hz\n", opdata->head.channel_count, 48000) ; //opus always outputs 48000
		psf_log_printf (psf, "Encoded by : %s\n", opdata->tags.vendor) ;

		/* Throw the comments plus a few lines about the bitstream we're decoding. */
		for (k = 0 ; k < ARRAY_LEN (ogg_opus_metatypes) ; k++)
		{	const char *dd ;

			dd = opus_tags_query (&opdata->tags, ogg_opus_metatypes [k].name, 0) ;
			if (dd == NULL)
				continue ;

			if (printed_metadata_msg == 0)
			{	psf_log_printf (psf, "Metadata :\n") ;
				printed_metadata_msg = 1 ;
			} ;

			psf_store_string (psf, ogg_opus_metatypes [k].id, dd) ;
			psf_log_printf (psf, "  %-10s : %s\n", ogg_opus_metatypes [k].name, dd) ;
		} ;

		psf_log_printf (psf, "End\n") ;
	} ;

	psf->sf.samplerate = 48000 ; //opus always outputs 48000
	psf->sf.channels = opdata->head.channel_count ;
	psf->sf.format = SF_FORMAT_OGG | SF_FORMAT_OPUS ;

	return 0 ;
} /* ogg_opus_read_header */

static int
ogg_opus_close (SF_PRIVATE * psf)
{
	OPUS_PRIVATE *opdata = (OPUS_PRIVATE *) psf->codec_data ;
	opus_tags_clear (&opdata->tags) ;

	return 0 ;
} /* ogg_opus_close */


static int
ogg_opus_byterate (SF_PRIVATE *psf)
{
	if (psf->file.mode == SFM_READ)
		return (psf->datalength * psf->sf.samplerate) / psf->sf.frames ;

	return -1 ;
} /* ogg_opus_byterate */

/*==============================================================================
**	The following code is based on the ogg_vorbis code, which  was snipped from
**	Mike Smith's ogginfo utility which is part of vorbis-tools.
**	Vorbis tools is released under the GPL but Mike has kindly allowed the
**	following to be relicensed as LGPL for libsndfile.
*/

typedef struct
{
	int isillegal ;
	int shownillegal ;
	int isnew ;
	int end ;

	uint32_t serial ; /* must be 32 bit unsigned */
	ogg_stream_state ostream ;

	sf_count_t lastgranulepos ;
	int doneheaders ;
} stream_processor ;

typedef struct
{
	stream_processor *streams ;
	int allocated ;
	int used ;
	int in_headers ;
} stream_set ;

static stream_set *
create_stream_set (void)
{	stream_set *set = calloc (1, sizeof (stream_set)) ;

	set->streams = calloc (5, sizeof (stream_processor)) ;
	set->allocated = 5 ;
	set->used = 0 ;

	return set ;
} /* create_stream_set */

static void
ogg_opus_end (stream_processor *stream, sf_count_t * len)
{	*len += stream->lastgranulepos ;
} /* ogg_opus_end */

static void
free_stream_set (stream_set *set, sf_count_t * len)
{	int i ;

	for (i = 0 ; i < set->used ; i++)
	{	if (!set->streams [i].end)
			ogg_opus_end (&set->streams [i], len) ;
		ogg_stream_clear (&set->streams [i].ostream) ;
		} ;

	free (set->streams) ;
	free (set) ;
} /* free_stream_set */

static int
streams_open (stream_set *set)
{	int i, res = 0 ;

	for (i = 0 ; i < set->used ; i++)
		if (!set->streams [i].end)
			res ++ ;
	return res ;
} /* streams_open */

static stream_processor *
find_stream_processor (stream_set *set, ogg_page *page)
{	uint32_t serial = ogg_page_serialno (page) ;
	int i, invalid = 0 ;

	stream_processor *stream ;

	for (i = 0 ; i < set->used ; i++)
	{
		if (serial == set->streams [i].serial)
		{	/* We have a match! */
			stream = & (set->streams [i]) ;

			set->in_headers = 0 ;
			/* if we have detected EOS, then this can't occur here. */
			if (stream->end)
			{	stream->isillegal = 1 ;
				return stream ;
				}

			stream->isnew = 0 ;
			stream->end = ogg_page_eos (page) ;
			stream->serial = serial ;
			return stream ;
			} ;
		} ;

	/* If there are streams open, and we've reached the end of the
	** headers, then we can't be starting a new stream.
	** XXX: might this sometimes catch ok streams if EOS flag is missing,
	** but the stream is otherwise ok?
	*/
	if (streams_open (set) && !set->in_headers)
		invalid = 1 ;

	set->in_headers = 1 ;

	if (set->allocated < set->used)
		stream = &set->streams [set->used] ;
	else
	{	set->allocated += 5 ;
		set->streams = realloc (set->streams, sizeof (stream_processor) * set->allocated) ;
		stream = &set->streams [set->used] ;
		} ;

	set->used++ ;

	stream->isnew = 1 ;
	stream->isillegal = invalid ;

	{
		int res ;
		ogg_packet packet ;

		/* We end up processing the header page twice, but that's ok. */
		ogg_stream_init (&stream->ostream, serial) ;
		ogg_stream_pagein (&stream->ostream, page) ;
		res = ogg_stream_packetout (&stream->ostream, &packet) ;
		if (res <= 0)
			return NULL ;
		else if (packet.bytes >= 8 && memcmp (packet.packet, "OpusHead", 8) == 0)
		{
			stream->lastgranulepos = 0 ;
			} ;

		res = ogg_stream_packetout (&stream->ostream, &packet) ;

		/* re-init, ready for processing */
		ogg_stream_clear (&stream->ostream) ;
		ogg_stream_init (&stream->ostream, serial) ;
	}

	stream->end = ogg_page_eos (page) ;
	stream->serial = serial ;

	return stream ;
} /* find_stream_processor */

static sf_count_t
ogg_opus_length_aux (SF_PRIVATE * psf)
{
	ogg_sync_state osync ;
	ogg_page page ;
	sf_count_t len = 0 ;
	stream_set *processors ;

	processors = create_stream_set () ;
	if (processors == NULL)
		return 0 ;	// out of memory?

	ogg_sync_init (&osync) ;

	while (ogg_opus_get_next_page (psf, &osync, &page))
	{
		stream_processor *p = find_stream_processor (processors, &page) ;

		if (!p)
		{	len = 0 ;
			break ;
			} ;

		if (p->isillegal && !p->shownillegal)
		{
			p->shownillegal = 1 ;
			/* If it's a new stream, we want to continue processing this page
			** anyway to suppress additional spurious errors
			*/
			if (!p->isnew) continue ;
			} ;

		if (!p->isillegal)
		{	ogg_packet packet ;
			int header = 0 ;

			ogg_stream_pagein (&p->ostream, &page) ;
			if (p->doneheaders < 2)
				header = 1 ;

			while (ogg_stream_packetout (&p->ostream, &packet) > 0)
			{
				if (p->doneheaders < 2)
				{	// just assume the first 2 packets are headers
					p->doneheaders ++ ;
					} ;
				} ;
			if (!header)
			{	sf_count_t gp = ogg_page_granulepos (&page) ;
				if (gp > 0) p->lastgranulepos = gp ;
				} ;
			if (p->end)
			{	ogg_opus_end (p, &len) ;
				p->isillegal = 1 ;
				} ;
			} ;
		} ;

	ogg_sync_clear (&osync) ;
	free_stream_set (processors, &len) ;

	return len ;
} /* ogg_opus_length_aux */

static sf_count_t
ogg_opus_length (SF_PRIVATE *psf)
{	sf_count_t length ;
	int error ;

	if (psf->sf.seekable == 0)
		return SF_COUNT_MAX ;

	psf_fseek (psf, 0, SEEK_SET) ;
	length = ogg_opus_length_aux (psf) ;

	psf_fseek (psf, 12, SEEK_SET) ;
	if ((error = ogg_opus_read_header (psf, 0)) != 0)
		psf->error = error ;

	return length ;
} /* ogg_opus_length */


#else /* ENABLE_EXPERIMENTAL_CODE && HAVE_EXTERNAL_XIPH_LIBS */

int
ogg_opus_open (SF_PRIVATE *psf)
{
	psf_log_printf (psf, "This version of libsndfile was compiled without Ogg/Opus support.\n") ;
	return SFE_UNIMPLEMENTED ;
} /* ogg_opusopen */

#endif
