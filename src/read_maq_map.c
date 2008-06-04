/* 
   Code to read in a .map file produced by the alignment program Maq.
   Authr: Simon Anders, EBI, sanders@fs.tum.de
*/

#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <R.h>
#include <Rinternals.h>
#include <zlib.h>
#include "ShortRead.h"
#include "maqmap.h"

#if INT_MAX < 0x7fffffffL
#error This package needs an int type with at least 32 bit.
#endif   

SEXP read_maq_map( SEXP filename, SEXP maxreads )
/* Reads in the Maq map file with the given filename. If maxreads == -1, the whole file
   is read, otherwise at most the specified number of reads. The fucntion returns a list 
   (i.e., a VECSXP) with the elements listed below in eltnames, which correspond to the 
   columns of maq mapview. */
{
    static const int lbuf = 500;
    char buf[lbuf];
    gzFile mapfile;   
    maqmap_t * mapheader;
    SEXP seqnames, seq, start, dir, length, aq, mm, mm24, errsum, nhits0, 
        nhits1, eltnm, df, klass, levels;
    char readseqbuf[ MAX_READLEN ], fastqbuf[ MAX_READLEN ];
    CharBBuf readid, readseq, fastq;
    int i, actnreads, j;
    maqmap1_t read;

    static const char *eltnames[] = {
        "chromosome", "position", "strand", "alignQuality",
        "nMismatchBestHit", "nMismatchBestHit24", "mismatchQuality",
        "nExactMatch24", "nOneMismatch24", "readId", "readSequence",
        "fastqScores"
    };
   
    /* Check arguments */
    if( !isString(filename) || length(filename) != 1 )
        error( "First argument invalid: should be the filename." );
    if( !isInteger(maxreads) || length(maxreads) != 1 )
        error( "Second argument invalid: should be the maximum number"
               "of reads, provided as integer(1)." );

    /* Check that file can be opened and is a Maq map file */
    mapfile = gzopen( CHAR(STRING_ELT(filename,0)), "rb" );   
    if( !mapfile ) {
        if( errno ) {
            error( "Failed to open file '%s': %s (errno=%d)",
                   CHAR(STRING_ELT(filename,0)), strerror(errno), errno );
        } else {
            error( "Failed to open file '%s':"
                   " zlib out of memory", CHAR(STRING_ELT(filename,0)));
        }
    }	    
    gzread( mapfile, &i, sizeof(int) );
    if( i != MAQMAP_FORMAT_NEW ) {
        gzclose( mapfile );
        error( "File '%s' is not a MAQ map file", 
               CHAR(STRING_ELT(filename,0)));
    }
    i = gzrewind( mapfile );
    assert( !i );

   
    /* Read in header and map maqfile sequence indices to veclist indices */
    mapheader =  maqmap_read_header( mapfile );   
    PROTECT( seqnames = allocVector( STRSXP, mapheader->n_ref ) );
    for( i = 0; i < mapheader->n_ref; i++ ) {
        SET_STRING_ELT( seqnames, i, mkChar( mapheader->ref_name[i] ) );
    }
    if( INTEGER(maxreads)[0] < 0 || 
        INTEGER(maxreads)[0] >= mapheader->n_mapped_reads )
        actnreads = mapheader->n_mapped_reads;
    else
        actnreads = INTEGER(maxreads)[0];
    maq_delete_maqmap(mapheader);      
   
    /* Allocate memory */
    PROTECT( seq    = allocVector( INTSXP, actnreads ) );
    PROTECT( start  = allocVector( INTSXP, actnreads ) );
    PROTECT( dir    = allocVector( INTSXP, actnreads ) );
    PROTECT( aq     = allocVector( INTSXP, actnreads ) );
    PROTECT( mm     = allocVector( INTSXP, actnreads ) );
    PROTECT( mm24   = allocVector( INTSXP, actnreads ) );
    PROTECT( errsum = allocVector( INTSXP, actnreads ) );
    PROTECT( nhits0 = allocVector( INTSXP, actnreads ) );
    PROTECT( nhits1 = allocVector( INTSXP, actnreads ) );
    readid  = new_CharBBuf( actnreads, 0 );
    readseq = new_CharBBuf( actnreads, 0 );
    fastq   = new_CharBBuf( actnreads, 0 );

    for( i = 0; i < actnreads; i++ ) {
      
        /* Various checks */
        if( gzeof(mapfile) ) {
            error( "Unexpected end of file." );
            gzclose(mapfile);
        }	 
        maqmap_read1( mapfile, &read );
        if( read.flag || read.dist ) {
            error( "Paired read found. This function cannot deal with paired reads (yet)." );
            gzclose(mapfile);
        }
      
        /* Build the read sequence and the FASTQ quality string */
        assert( read.size <= MAX_READLEN );
        for (j = 0; j < read.size; j++) {
            if (read.seq[j] == 0)
                readseqbuf[j] = 'N';
            else 
                readseqbuf[j] = "ACGT"[ read.seq[j] >> 6 & 0x03 ];
            fastqbuf[j] = ( read.seq[j] & 0x3f ) + 33;   
        }
        readseqbuf[ read.size ] = 0;
        fastqbuf  [ read.size ] = 0;      
      
        /* Copy the data */
        INTEGER(start)[i] = ( read.pos >> 1 ) + 1;
        INTEGER(dir  )[i] = !( read.pos & 0x01 ) + 1; /* '-': 1, '+': 2 */
        INTEGER(seq   )[i] = read.seqid + 1;
        INTEGER(aq    )[i] = read.map_qual;
        INTEGER(mm    )[i] = read.info1 & 0x0f;
        INTEGER(mm24  )[i] = read.info1 >> 4;
        INTEGER(errsum)[i] = read.info2;
        INTEGER(nhits0)[i] = read.c[0];
        INTEGER(nhits1)[i] = read.c[1];
        append_string_to_CharBBuf( &readid,  read.name );
        append_string_to_CharBBuf( &readseq, readseqbuf );
        append_string_to_CharBBuf( &fastq,   fastqbuf );
    }
   
    /* Build the data frame */
    PROTECT( df = allocVector( VECSXP, 12 ) );
    SET_VECTOR_ELT( df, 0, seq );
    SET_VECTOR_ELT( df, 1, start );
    SET_VECTOR_ELT( df, 2, dir );    
    SET_VECTOR_ELT( df, 3, aq );
    SET_VECTOR_ELT( df, 4, mm );    
    SET_VECTOR_ELT( df, 5, mm24 );  
    SET_VECTOR_ELT( df, 6, errsum );
    SET_VECTOR_ELT( df, 7, nhits0 );
    SET_VECTOR_ELT( df, 8, nhits1 );
    SET_VECTOR_ELT( df, 9, new_XStringSet_from_RoSeqs( "BString",   new_RoSeqs_from_BBuf( readid ) ) );
    SET_VECTOR_ELT( df, 10, new_XStringSet_from_RoSeqs( "DNAString", new_RoSeqs_from_BBuf( readseq ) ) );
    SET_VECTOR_ELT( df, 11, new_XStringSet_from_RoSeqs( "BString",   new_RoSeqs_from_BBuf( fastq ) ) );

    setAttrib( seq, install( "levels" ), seqnames );
    PROTECT( klass = allocVector( STRSXP, 1 ) );
    SET_STRING_ELT( klass, 0, mkChar( "factor" ) );
    setAttrib( seq, install( "class" ), klass );
    UNPROTECT( 1 );


    PROTECT( levels = allocVector( STRSXP, 2 ) );
    SET_STRING_ELT( levels, 0, mkChar( "-" ) );
    SET_STRING_ELT( levels, 1, mkChar( "+" ) );
    setAttrib( dir, install( "levels" ), levels );
    PROTECT( klass = allocVector( STRSXP, 1 ) );
    SET_STRING_ELT( klass, 0, mkChar( "factor" ) );
    setAttrib( dir, install( "class" ), klass );
    UNPROTECT( 1 );
   
    PROTECT( eltnm = allocVector( STRSXP, 12 ) );
    for( i = 0; i < 12; i++ )
        SET_STRING_ELT( eltnm, i, mkChar( eltnames[i] ) );
    namesgets( df, eltnm );

    UNPROTECT( 13 );
    return df;   
}   