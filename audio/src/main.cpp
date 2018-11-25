#include <stdio.h>
#include <windows.h>
#include <DSound.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <complex>
#define  APP_IMPLEMENTATION
#define  APP_WINDOWS
#include <stdlib.h> // for rand and __argc/__argv
#include <string.h> // for memset
#include "..\libs\app.h"
#include "..\libs\graph.h"
#define SYSFONT_IMPLEMENTATION
#include "..\libs\sysfont.h"

#define M_PI 3.14159265358979323846 // Pi constant with double precision
#define BUFFER_SAMPLE_SIZE 8192
#define BYTES_PER_SAMPLE 2
#define DSOUND_BUFFER_BYTE_SIZE ( BUFFER_SAMPLE_SIZE * BYTES_PER_SAMPLE * 2)


enum tone_t
{
	TONE_E = 0,
	TONE_F = 1,
	TONE_Fs = 2,
	TONE_G = 3,
	TONE_Gs = 4,
	TONE_A = 5,
	TONE_As = 6,
	TONE_B = 7,
	TONE_C = 8,
	TONE_Cs = 9,
	TONE_D = 10,
	TONE_Ds = 11,
};

enum string_t
{
	STRING_INVALID = 0,
	STRING_LOW_E = 1,
	STRING_A = 2,
	STRING_D = 3,
	STRING_G = 4,
	STRING_B = 5,
	STRING_HIGH_E = 6,
};

struct note_position_t
{
	int fret;
	string_t string;
};

struct note_t
{
	float frequency;
	tone_t tone;
	int octave;
	note_position_t positions[ 6 ];
};

#pragma comment(lib, "DSound.lib")
#pragma comment(lib, "dxguid.lib")
void line( int x1, int y1, int x2, int y2, APP_U32* canvas )
{
	int dx = x2 - x1;
	dx = dx < 0 ? -dx : dx;
	int sx = x1 < x2 ? 1 : -1;
	int dy = y2 - y1;
	dy = dy < 0 ? -dy : dy;
	int sy = y1 < y2 ? 1 : -1;
	int err = (dx > dy ? dx : -dy) / 2;
	//APP_U32 color = rand() | ((APP_U32)rand() << 16);
	APP_U32 color = 0xff;

	int x = x1;
	int y = y1;
	while( x != x2 || y != y2 )
	{
		canvas[ x + y * 1920 ] = color;

		int e2 = err;
		if (e2 > -dx) { err -= dy; x += sx; }
		if (e2 < dy) { err += dx; y += sy; }
	}
	canvas[ x + y * 1920 ] = color;

}

// separate even/odd elements to lower/upper halves of array respectively.
// Due to Butterfly combinations, this turns out to be the simplest way 
// to get the job done without clobbering the wrong elements.
void separate( std::complex<double>* a, int n )
{
	std::complex<double>* b = new std::complex<double>[ n / 2 ];  // get temp heap storage
	for( int i = 0; i < n / 2; i++ )    // copy all odd elements to heap storage
		b[ i ] = a[ i * 2 + 1 ];
	for( int i = 0; i < n / 2; i++ )    // copy all even elements to lower-half of a[]
		a[ i ] = a[ i * 2 ];
	for( int i = 0; i < n / 2; i++ )    // copy all odd (from heap) to upper-half of a[]
		a[ i + n / 2 ] = b[ i ];
	delete[] b;                 // delete heap storage
}

// N must be a power-of-2, or bad things will happen.	
// Currently no check for this condition.
//
// N input samples in X[] are FFT'd and results left in X[].
// Because of Nyquist theorem, N samples means 
// only first N/2 FFT results in X[] are the answer.
// (upper half of X[] is a reflection with no new information).
void fft2( std::complex<double>* X, int N ) 
{
	if( N < 2 )
	{
		// bottom of recursion.
		// Do nothing here, because already X[0] = x[0]
	}
	else
	{
		separate( X, N );      // all evens to lower half, all odds to upper half
		fft2( X, N / 2 );   // recurse even items
		fft2( X + N / 2, N / 2 );   // recurse odd  items
		// combine results of two half recursions
		for( int k = 0; k < N / 2; k++ ) 
		{
			std::complex<double> e = X[ k ];   // even
			std::complex<double> o = X[ k + N / 2 ];   // odd
			 // w is the "twiddle-factor"
			std::complex<double> w = exp( std::complex<double>( 0, -2.*M_PI*k / N ) );
			X[ k ] = e + w * o;
			X[ k + N / 2 ] = e - w * o;
		}
	}
}


BOOL CALLBACK DSEnumCallback( LPGUID lpGuid, LPCSTR lpcstrDescription, LPCSTR lpcstrModule, LPVOID lpContext )
{
	if( !lpGuid ) return true;
	LPGUID* guid = (LPGUID*)lpContext;
	if( strstr( lpcstrDescription, "Rocksmith" ) != 0 )
	{
		*guid = lpGuid;
		return FALSE;
	}

	return TRUE;
}

HRESULT CreateCaptureBuffer( LPDIRECTSOUNDCAPTURE8 pDSC, LPDIRECTSOUNDCAPTUREBUFFER8* ppDSCB8 )
{
	HRESULT hr;
	DSCBUFFERDESC               dscbd;
	LPDIRECTSOUNDCAPTUREBUFFER  pDSCB;
	WAVEFORMATEX                wfx =
	{ WAVE_FORMAT_PCM, 1, 44100, 88200, 2, 16, 0 };
	// wFormatTag, nChannels, nSamplesPerSec, mAvgBytesPerSec, nBlockAlign, wBitsPerSample, cbSize

	if( (NULL == pDSC) || (NULL == ppDSCB8) ) return E_INVALIDARG;
	dscbd.dwSize = sizeof( DSCBUFFERDESC );
	dscbd.dwFlags = 0;
	dscbd.dwBufferBytes = DSOUND_BUFFER_BYTE_SIZE;
	dscbd.dwReserved = 0;
	dscbd.lpwfxFormat = &wfx;
	dscbd.dwFXCount = 0;
	dscbd.lpDSCFXDesc = NULL;

	if( SUCCEEDED( hr = pDSC->CreateCaptureBuffer( &dscbd, &pDSCB, NULL ) ) )
	{
		hr = pDSCB->QueryInterface( IID_IDirectSoundCaptureBuffer8, (LPVOID*)ppDSCB8 );
		pDSCB->Release();
	}
	else
	{
		DWORD error = GetLastError();
		(void)error;
		return hr;
	}
	return hr;
}


int app_proc(app_t* app, void* user_data)
{
	const int SCREEN_HEIGHT = 1080;
	const int SCREEN_WIDTH = 1920;
	const int number_of_notes = 49;
	note_t notes[ number_of_notes ] = { 0 };

	for( int j = TONE_E; j <= TONE_Ds; ++j )
	{
		tone_t tone = (tone_t)j;
		float base_freq = 0;
		float max_freq = 1318.51f;
		switch( tone )
		{
			case TONE_E: base_freq = 82.4069f; break;
			case TONE_F: base_freq = 87.3071f; break;
			case TONE_Fs: base_freq = 92.4986f; break;
			case TONE_G: base_freq = 97.9989f; break;
			case TONE_Gs: base_freq = 103.826f; break;
			case TONE_A: base_freq = 110.0f; break;
			case TONE_As: base_freq = 116.541f; break;
			case TONE_B: base_freq = 123.471f; break;
			case TONE_C: base_freq = 130.813f; break;
			case TONE_Cs: base_freq = 138.591f; break;
			case TONE_D: base_freq = 146.832f; break;
			case TONE_Ds: base_freq = 155.563f; break;
			default: assert( false );
		}
			
		for( int octave = 0; octave < 5; ++octave ) // maximum number of octaves
		{
			if( base_freq > 1318.52f ) break;
			if( tone + (octave * 11) >= number_of_notes ) break;

			// create note entry
			notes[ tone + ( octave * 12 ) ].frequency = base_freq;
			notes[ tone + ( octave * 12 ) ].octave = octave;
			notes[ tone + ( octave * 12 ) ].tone = tone;
			base_freq *= 2;
		}
	}


	// find all positions on fretboard, string by string
	for( int string = STRING_LOW_E; string <= STRING_HIGH_E; ++string )
	{
		int start_note_position = 5 * ( string - 1 );
		if( string > STRING_G ) --start_note_position;
		for( int fret = 0; fret < 25; ++fret )
		{
			int position = 0;
			for( int position = 0; position < 6; ++position )
			{
				if( notes[ start_note_position + fret ].positions[ position ].string == STRING_INVALID )
				{
					notes[ start_note_position + fret ].positions[ position ].fret = fret;
					notes[ start_note_position + fret ].positions[ position ].string = (string_t)string;
					break;
				}
			}
		}
	}


	(void)user_data;
	APP_U32* canvas = (APP_U32*)malloc( sizeof( APP_U32 ) * SCREEN_HEIGHT * SCREEN_WIDTH );
	memset( canvas, 0xC0, sizeof( APP_U32 ) * SCREEN_HEIGHT * SCREEN_WIDTH ); // clear to grey
	app_screenmode( app, APP_SCREENMODE_WINDOW );

	LPGUID guid;
	LPDIRECTSOUNDCAPTUREBUFFER8 buffer;
	DSBPOSITIONNOTIFY buffer_notification[ 2 ];
	HANDLE on_event = CreateEvent( NULL, TRUE, FALSE, NULL );

	memset( buffer_notification, 0, sizeof( buffer_notification ) );

	bool read_mic = true;
	if( read_mic )
	{
		HRESULT hr = DirectSoundCaptureEnumerate( (LPDSENUMCALLBACK)DSEnumCallback, (VOID*)&guid );

		LPDIRECTSOUNDCAPTURE8 lpds;
		LPDIRECTSOUNDNOTIFY notification = NULL;
		hr = DirectSoundCaptureCreate8( guid, &lpds, NULL );
		CreateCaptureBuffer( lpds, &buffer );

		if( FAILED( hr = buffer->QueryInterface( IID_IDirectSoundNotify,
			(VOID**)&notification ) ) ){ }

		buffer_notification[ 0 ].dwOffset = ( DSOUND_BUFFER_BYTE_SIZE / 2 ) - 1;
		buffer_notification[ 0 ].hEventNotify = on_event;
		buffer_notification[ 1 ].dwOffset = DSOUND_BUFFER_BYTE_SIZE - 1;
		buffer_notification[ 1 ].hEventNotify = on_event;
		notification->SetNotificationPositions( 2, buffer_notification );
		buffer->Start( DSCBSTART_LOOPING );

	}

	static uint16_t data_buffer[ BUFFER_SAMPLE_SIZE ];
	static std::complex<double> fft_buffer[ BUFFER_SAMPLE_SIZE * 2 ];
	static std::complex<double> result_buffer[ BUFFER_SAMPLE_SIZE * 2 ];
	static double amplitude[ BUFFER_SAMPLE_SIZE * 2 ];
	static double han_window_multiplier[ BUFFER_SAMPLE_SIZE * 2 ];

	// create han window
	for( int i = 0; i < BUFFER_SAMPLE_SIZE * 2; ++i )
		han_window_multiplier[ i ] = 0.5 * (1 - cos( 2 * M_PI * i / ((BUFFER_SAMPLE_SIZE * 2) - 1.0) ));

	// keep running until the user close the window
	int ack = 82;
	while( app_yield( app ) != APP_STATE_EXIT_REQUESTED )
	{
		// 44100 * 0.05 * 0.5 = ~1100hz högsta frekvens att detecta
		// ca 2205 samples
		// bin size = 44100 / 2205 = 20hz (50ms)
		// bin size = 44100 / 8820 = 5hz (200ms)
		int nr_samples = 0;
		if( read_mic )
		{

			DWORD wait_result = WaitForMultipleObjects( 1, &on_event, false, 2000 );
			if( wait_result != WAIT_OBJECT_0 ) continue;

			DWORD current_pos;
			DWORD hr;
			if( FAILED( hr = buffer->GetCurrentPosition( 0, &current_pos ) ) )
			{
				assert( false );
			}

			DWORD read_pos = current_pos <= ( DSOUND_BUFFER_BYTE_SIZE - 1 ) ? ( DSOUND_BUFFER_BYTE_SIZE - 1 ) : 0;

			VOID* first_block = 0;
			DWORD first_block_size = 0;
			VOID* second_block = 0;
			DWORD second_block_size = 0;
			buffer->Lock( read_pos, DSOUND_BUFFER_BYTE_SIZE / 2, &first_block, &first_block_size, &second_block, &second_block_size, 0 );
			
			memcpy( (uint8_t*)data_buffer, first_block, (int)first_block_size );
			if( second_block ) memcpy( (uint8_t*)data_buffer + (int)first_block_size, second_block, (int)second_block_size );

			buffer->Unlock( first_block, first_block_size, second_block, second_block_size );
			nr_samples = (first_block_size + second_block_size) / 2;

			for( int i = 0; i < nr_samples; ++i )
			{
				fft_buffer[ i ] = (double)(data_buffer[ i ]);// * han_window_multiplier[ i ];
				result_buffer[ i ] = (double)(data_buffer[ i ]) * han_window_multiplier[ i ];
			}
		}
		else
		{
			Sleep( 100 );
			nr_samples = BUFFER_SAMPLE_SIZE;
			for( int i = 0; i < nr_samples; ++i )
			{
				double step = (1.0f / 44100.0f) * i;
				fft_buffer[ i ] = cos( 2 * M_PI * 83 * step ) * han_window_multiplier[ i ];

				result_buffer[ i ] = fft_buffer[ i ];
			}
			ack += 10;
			if( ack >= 1500 ) ack = 82;
		}

		if( nr_samples == 0 ) continue;
		fft2( result_buffer, nr_samples );
		double bin_size = 44100.0f / nr_samples;
		double max_amp = 0;
		int index_max = 0;
		int start_index = (floor( 82.4069f / bin_size ));
		int end_index = (ceil( 1318.51f / bin_size )) <= (nr_samples / 2) ? (ceil( 1318.51f / bin_size )) : nr_samples / 2;
		for( int i = start_index; i < end_index; i++ )
		{
			amplitude[ i ] = sqrt( (result_buffer[ i ].real() * result_buffer[ i ].real()) + (result_buffer[ i ].imag() * result_buffer[ i ].imag()) );
			if( amplitude[ i ] > max_amp && amplitude[ i ] > 7500000 )
			{
				index_max = i;
				max_amp = amplitude[ i ];
			}
		}

		double max_freq = bin_size * (index_max);
		int active_note_index = -1;
		for( int i = 0; i < number_of_notes; ++i )
		{
			if( abs( notes[ i ].frequency - max_freq ) <= bin_size )
			{
				active_note_index = i;
				break;
			}
		}
		memset( canvas, 0xC0, sizeof( APP_U32 ) * SCREEN_HEIGHT * SCREEN_WIDTH ); // clear to grey

		// draw strings
		int string_space = 50;
		for( int i = 0; i < 7; ++i )
		{
			line( 70, 
				(SCREEN_HEIGHT / 2) - (i * string_space), 
				SCREEN_WIDTH - 20, 
				(SCREEN_HEIGHT / 2) - (i * string_space), 
				canvas );
		}

		// draw frets
		int fret_space = ( SCREEN_WIDTH - 20 - 70 ) / 25;
		for( int i = 0; i < 25; ++i )
		{
			line( 70 + fret_space * ( i + 1 ),
				SCREEN_HEIGHT / 2,
				70 + fret_space * ( i + 1 ),
				( SCREEN_HEIGHT / 2 ) - string_space * 6,
				canvas );
		}

		// draw notes
		for( int i = 0; i < number_of_notes; ++i )
		{
			char buffer[ 4 ] = { 0 };
			switch( notes[ i ].tone )
			{
				case TONE_E: sprintf( buffer, "E%i", notes[ i ].octave ); break;
				case TONE_F: sprintf( buffer, "F%i", notes[ i ].octave ); break;
				case TONE_Fs: sprintf( buffer, "F#%i", notes[ i ].octave ); break;
				case TONE_G: sprintf( buffer, "G%i", notes[ i ].octave ); break;
				case TONE_Gs: sprintf( buffer, "G#%i", notes[ i ].octave ); break;
				case TONE_A: sprintf( buffer, "A%i", notes[ i ].octave ); break;
				case TONE_As: sprintf( buffer, "A#%i", notes[ i ].octave ); break;
				case TONE_B: sprintf( buffer, "B%i", notes[ i ].octave ); break;
				case TONE_C: sprintf( buffer, "C%i", notes[ i ].octave ); break;
				case TONE_Cs: sprintf( buffer, "C#%i", notes[ i ].octave ); break;
				case TONE_D: sprintf( buffer, "D%i", notes[ i ].octave ); break;
				case TONE_Ds: sprintf( buffer, "D#%i", notes[ i ].octave ); break;
			}

			for( int position_index = 0; position_index <= 6; ++position_index )
			{
				if( notes[ i ].positions[ position_index ].string == STRING_INVALID ) break;
				sysfont_9x16_u32( canvas, SCREEN_WIDTH, SCREEN_HEIGHT,
					70 + fret_space * (notes[ i ].positions[ position_index ].fret),
					(SCREEN_HEIGHT / 2) - (notes[ i ].positions[ position_index ].string * string_space),
					buffer, active_note_index == i ? 0x00ffff00 : 0x00ff0000 );
			}
		}

		// draw top frequency
		{
			char buffer[ 50 ] = { 0 };
			sprintf( buffer, "%f - %f", max_freq, (max_freq + bin_size) );
			sysfont_9x16_u32( canvas, SCREEN_WIDTH, SCREEN_HEIGHT, 100, 200, buffer, 0x00ff0000 );
		}

		// draw frequency domain
		for( int i = start_index; i < end_index; ++i )
		{
			line( i * 6 + 20, 
				1080 - 20,
				i * 6 + 20,
				(1080 - 20 - amplitude[ i ] * 0.00001) < 0 ? 20 : (1080 - 20 - amplitude[ i ] * 0.00001), canvas );
		}

		app_present( app, canvas, SCREEN_WIDTH, SCREEN_HEIGHT, 0xffffff, 0x000000 );
	}
	return 0;
}

int main(int argc, char** argv)
{
	(void)argc, argv;
	return app_run( app_proc, NULL, NULL, NULL, NULL );
}
// pass-through so the program will build with either /SUBSYSTEM:WINDOWS or /SUBSYSTEN:CONSOLE
extern "C" int __stdcall WinMain( struct HINSTANCE__*, struct HINSTANCE__*, char*, int ) { return main( __argc, __argv ); }