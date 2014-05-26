/* Nes.cpp - A NES instrument plugin for LMMS
 *                        
 * Copyright (c) 2014 Vesa Kivimäki
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of Linux MultiMedia Studio - http://lmms.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include <QtXml/QDomElement>

#include "Nes.h"
#include "engine.h"
#include "InstrumentTrack.h"
#include "templates.h"
#include "tooltip.h"
#include "song.h"
#include "lmms_math.h"
#include "interpolation.h"
#include "Oscillator.h"

#include "embed.cpp"

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT nes_plugin_descriptor =
{
	STRINGIFY( PLUGIN_NAME ),
	"Nescaline",
	QT_TRANSLATE_NOOP( "pluginBrowser",
				"A NES-like synthesizer" ),
	"Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>",
	0x0100,
	Plugin::Instrument,
	new PluginPixmapLoader( "logo" ),
	NULL,
	NULL
} ;

}


NesObject::NesObject( NesInstrument * nes, const sample_rate_t samplerate, NotePlayHandle * nph, fpp_t frames ) :
	m_parent( nes ),
	m_samplerate( samplerate ),
	m_nph( nph ),
	m_fpp( frames )
{
	m_LFSR = LFSR_INIT;
	
	m_ch1Counter = 0;
	m_ch2Counter = 0;
	m_ch3Counter = 0;
	m_ch4Counter = 0;
	
	m_ch1EnvCounter = 0;
	m_ch2EnvCounter = 0;
	m_ch4EnvCounter = 0;
	
	m_ch1EnvValue = 15;
	m_ch2EnvValue = 15;
	m_ch4EnvValue = 15;
	
	m_ch1SweepCounter = 0;
	m_ch2SweepCounter = 0;
	
	m_12Last = 0.0f;
	m_34Last = 0.0f;
	
	m_itm = 0.0f;
	m_otm = 0.0f;
	
	m_lastNoteFreq = 0.0f;
	
	m_maxWlen = wavelength( MIN_FREQ );
	
	m_nsf = NES_SIMPLE_FILTER * ( m_samplerate / 44100.0 );
}


NesObject::~NesObject()
{
}


void NesObject::renderOutput( sampleFrame * buf, fpp_t frames )
{
	// check if frequency has changed, if so, update wavelengths of ch1-3
	if( m_nph->frequency() != m_lastNoteFreq )
	{
		m_wlen1 = wavelength( m_nph->frequency() * m_parent->m_freq1 );
		m_wlen2 = wavelength( m_nph->frequency() * m_parent->m_freq2 );
		m_wlen3 = wavelength( m_nph->frequency() * m_parent->m_freq3 );
	}
	// noise channel can use either note freq or preset freqs
	if( m_parent->m_ch4NoiseFreqMode.value() )
	{
		m_wlen4 = wavelength( m_nph->frequency() );
	}
	else
	{
		m_wlen4 = wavelength( NOISE_FREQS[ 15 - static_cast<int>( m_parent->m_ch4NoiseFreq.value() ) ] );
	}
	
	m_lastNoteFreq = m_nph->frequency();
	
	////////////////////////////////
	//	                          //
	//  variables for processing  //
	//                            //
	////////////////////////////////
	
	bool ch1Enabled = m_parent->m_ch1Enabled.value();
	bool ch2Enabled = m_parent->m_ch2Enabled.value();
	bool ch3Enabled = m_parent->m_ch3Enabled.value();
	bool ch4Enabled = m_parent->m_ch4Enabled.value();
	
	float ch1DutyCycle = DUTY_CYCLE[ m_parent->m_ch1DutyCycle.value() ];
	int ch1EnvLen = wavelength( floorf( 240.0 / ( m_parent->m_ch1EnvLen.value() + 1 ) ) );
	bool ch1EnvLoop = m_parent->m_ch1EnvLooped.value();
	
	float ch2DutyCycle = DUTY_CYCLE[ m_parent->m_ch2DutyCycle.value() ];
	int ch2EnvLen = wavelength( floorf( 240.0 / ( m_parent->m_ch2EnvLen.value() + 1 ) ) );
	bool ch2EnvLoop = m_parent->m_ch2EnvLooped.value();
	
	int ch4EnvLen = wavelength( floorf( 240.0 / ( m_parent->m_ch4EnvLen.value() + 1 ) ) );
	bool ch4EnvLoop = m_parent->m_ch4EnvLooped.value();
	
	int ch1;
	int ch2;
	int ch3;
	int ch4;
	
	int ch1SweepRate = wavelength( floorf( 120.0 / ( m_parent->m_ch1SweepRate.value() + 1 ) ) );
	int ch2SweepRate = wavelength( floorf( 120.0 / ( m_parent->m_ch2SweepRate.value() + 1 ) ) );

	int ch1Sweep = static_cast<int>( m_parent->m_ch1SweepAmt.value() * -1.0 );
	int ch2Sweep = static_cast<int>( m_parent->m_ch2SweepAmt.value() * -1.0 );

	// the amounts are inverted so we correct them here
	if( ch1Sweep > 0 )
	{
		ch1Sweep = 8 - ch1Sweep;
	}
	if( ch1Sweep < 0 )
	{
		ch1Sweep = -8 - ch1Sweep;
	}

	if( ch2Sweep > 0 )
	{
		ch2Sweep = 8 - ch2Sweep;
	}
	if( ch2Sweep < 0 )
	{
		ch2Sweep = -8 - ch2Sweep;
	}

	
	// start framebuffer loop
	
	for( f_cnt_t f = 0; f < frames; f++ )
	{
		////////////////////////////////
		//	                          //
		//        channel 1           //
		//                            //
		////////////////////////////////

		// render pulse wave
		if( m_wlen1 <= m_maxWlen && m_wlen1 >= MIN_WLEN && ch1Enabled )
		{
			ch1 = m_ch1Counter > m_wlen1 * ch1DutyCycle 
				? 0
				: m_parent->m_ch1EnvEnabled.value()
						? static_cast<int>( ( m_parent->m_ch1Volume.value() * m_ch1EnvValue ) / 15.0 )
						: static_cast<int>( m_parent->m_ch1Volume.value() );
		}
		else ch1 = 0;
		
		// update sweep
		m_ch1SweepCounter++;
		if( m_ch1SweepCounter >= ch1SweepRate )
		{
			m_ch1SweepCounter = 0;
			if( m_parent->m_ch1SweepEnabled.value() && m_wlen1 <= m_maxWlen && m_wlen1 >= MIN_WLEN )
			{
				// check if the sweep goes up or down
				if( ch1Sweep > 0 )
				{
					m_wlen1 += m_wlen1 >> qAbs( ch1Sweep );
				}
				if( ch1Sweep < 0 )
				{
					m_wlen1 -= m_wlen1 >> qAbs( ch1Sweep );
					m_wlen1--;  // additional minus 1 for ch1 only
				}
			}
		}
					
		// update framecounters
		m_ch1Counter++;
		m_ch1Counter = m_ch1Counter % m_wlen1;

		m_ch1EnvCounter++;
		if( m_ch1EnvCounter >= ch1EnvLen )
		{
			m_ch1EnvCounter = 0;
			m_ch1EnvValue--;
			if( m_ch1EnvValue < 0 )
			{
				m_ch1EnvValue = ch1EnvLoop ? 15	: 0;
			}
		}
	

		////////////////////////////////
		//	                          //
		//        channel 2           //
		//                            //
		////////////////////////////////

		// render pulse wave
		if( m_wlen2 <= m_maxWlen && m_wlen2 >= MIN_WLEN && ch2Enabled )
		{
			ch2 = m_ch2Counter > m_wlen2 * ch2DutyCycle 
				? 0
				: m_parent->m_ch2EnvEnabled.value()
						? static_cast<int>( ( m_parent->m_ch2Volume.value() * m_ch2EnvValue ) / 15.0 )
						: static_cast<int>( m_parent->m_ch2Volume.value() );
		}
		else ch2 = 0;
		
		// update sweep
		m_ch2SweepCounter++;
		if( m_ch2SweepCounter >= ch2SweepRate )
		{
			m_ch2SweepCounter = 0;
			if( m_parent->m_ch2SweepEnabled.value() && m_wlen2 <= m_maxWlen && m_wlen2 >= MIN_WLEN )
			{				
				// check if the sweep goes up or down
				if( ch2Sweep > 0 )
				{
					m_wlen2 += m_wlen2 >> qAbs( ch2Sweep );
				}
				if( ch2Sweep < 0 )
				{
					m_wlen2 -= m_wlen2 >> qAbs( ch2Sweep );
				}
			}
		}
					
		// update framecounters
		m_ch2Counter++;
		m_ch2Counter = m_ch2Counter % m_wlen2;
		
		m_ch2EnvCounter++;
		if( m_ch2EnvCounter >= ch2EnvLen )
		{
			m_ch2EnvCounter = 0;
			m_ch2EnvValue--;
			if( m_ch2EnvValue < 0 )
			{
				m_ch2EnvValue = ch2EnvLoop
					? 15
					: 0;
			}
		}
		
		
		////////////////////////////////
		//	                          //
		//        channel 3           //
		//                            //
		////////////////////////////////		
		
		// make sure we don't overflow
		m_ch3Counter %= m_wlen3;
		
		// render triangle wave
		if( m_wlen3 <= m_maxWlen && ch3Enabled )
		{
			ch3 = TRIANGLE_WAVETABLE[ ( m_ch3Counter * 32 ) / m_wlen3 ];
			ch3 = ( ch3 * static_cast<int>( m_parent->m_ch3Volume.value() ) ) / 15;
		}
		else ch3 = 0;
		
		m_ch3Counter++;
		
		
		////////////////////////////////
		//	                          //
		//        channel 4           //
		//                            //
		////////////////////////////////			
		
		// render pseudo noise 
		if( ch4Enabled )
		{
			ch4 = LFSR()
				? ( m_parent->m_ch4EnvEnabled.value()
						? ( static_cast<int>( m_parent->m_ch4Volume.value() ) * m_ch4EnvValue ) / 15
						: static_cast<int>( m_parent->m_ch4Volume.value() ) )
				: 0;
		}
		else ch4 = 0;
		
		// update framecounters
		m_ch4Counter++;
		if( m_ch4Counter >= m_wlen4 )
		{
			m_ch4Counter = 0;
			updateLFSR( m_parent->m_ch4NoiseMode.value() );
		}
		m_ch4EnvCounter++;
		if( m_ch4EnvCounter >= ch4EnvLen )
		{
			m_ch4EnvCounter = 0;
			m_ch4EnvValue--;
			if( m_ch4EnvValue < 0 )
			{
				m_ch4EnvValue = ch4EnvLoop
					? 15
					: 0;
			}
		}
		

		////////////////////////////////
		//	                          //
		//  final stage - mixing      //
		//                            //
		////////////////////////////////			
		
		float ch12 = static_cast<float>( ch1 + ch2 ); 
		// add dithering noise
		ch12 *= 1.0 + ( Oscillator::noiseSample( 0.0f ) * DITHER_AMP );		
		ch12 = ch12 / 15.0f - 1.0f;
		
		ch12 = signedPow( ch12, NES_DIST );
		
		// simple first order iir filter, to simulate the frequency response falloff in nes analog audio output
		ch12 = linearInterpolate( ch12, m_12Last, m_nsf );
		m_12Last = ch12;

		
		ch12 *= NES_MIXING_12;

		
		float ch34 = static_cast<float>( ch3 + ch4 ); 
		// add dithering noise
		ch34 *= 1.0 + ( Oscillator::noiseSample( 0.0f ) * DITHER_AMP );		
		ch34 = ch34 / 15.0f - 1.0f;
		
		ch34 = signedPow( ch34, NES_DIST );

		// simple first order iir filter, to simulate the frequency response falloff in nes analog audio output
		ch34 = linearInterpolate( ch34, m_34Last, m_nsf );
		m_34Last = ch34;
		
		
		ch34 *= NES_MIXING_34;
		
		float mixdown = ( ch12 + ch34 ) * NES_MIXING_ALL * m_parent->m_masterVol.value();
		
		// dc offset removal
		m_otm = 0.999f * m_otm + mixdown - m_itm;
		m_itm = mixdown;
		buf[f][0] = m_otm;
		buf[f][1] = m_otm;
		
	} // end framebuffer loop

}


NesInstrument::NesInstrument( InstrumentTrack * instrumentTrack ) :
	Instrument( instrumentTrack, &nes_plugin_descriptor ),
	m_ch1Enabled( true, this ),
	m_ch1Crs( 0.f, -24.f, 24.f, 1.f, this, tr( "Channel 1 Coarse detune" ) ),
	m_ch1Volume( 15.f, 0.f, 15.f, 1.f, this, tr( "Channel 1 Volume" ) ),
	
	m_ch1EnvEnabled( false, this ),
	m_ch1EnvLooped( false, this ),
	m_ch1EnvLen( 0.f, 0.f, 15.f, 1.f, this, tr( "Channel 1 Envelope length" ) ),
	
	m_ch1DutyCycle( 0, 0, 3, this, tr( "Channel 1 Duty cycle" ) ),
	
	m_ch1SweepEnabled( false, this ),
	m_ch1SweepAmt( 0.f, -7.f, 7.f, 1.f, this, tr( "Channel 1 Sweep amount" ) ),
	m_ch1SweepRate( 0.f, 0.f, 7.f, 1.f, this, tr( "Channel 1 Sweep rate" ) ),
	
	m_ch2Enabled( true, this ),
	m_ch2Crs( 0.f, -24.f, 24.f, 1.f, this, tr( "Channel 2 Coarse detune" ) ),
	m_ch2Volume( 15.f, 0.f, 15.f, 1.f, this, tr( "Channel 2 Volume" ) ),
	
	m_ch2EnvEnabled( false, this ),
	m_ch2EnvLooped( false, this ),
	m_ch2EnvLen( 0.f, 0.f, 15.f, 1.f, this, tr( "Channel 2 Envelope length" ) ),
	
	m_ch2DutyCycle( 2, 0, 3, this, tr( "Channel 2 Duty cycle" ) ),
	
	m_ch2SweepEnabled( false, this ),
	m_ch2SweepAmt( 0.f, -7.f, 7.f, 1.f, this, tr( "Channel 2 Sweep amount" ) ),
	m_ch2SweepRate( 0.f, 0.f, 7.f, 1.f, this, tr( "Channel 2 Sweep rate" ) ),
	
	//channel 3
	m_ch3Enabled( true, this ),
	m_ch3Crs( 0.f, -24.f, 24.f, 1.f, this, tr( "Channel 3 Coarse detune" ) ),
	m_ch3Volume( 15.f, 0.f, 15.f, 1.f, this, tr( "Channel 3 Volume" ) ),

	//channel 4
	m_ch4Enabled( true, this ),
	m_ch4Volume( 15.f, 0.f, 15.f, 1.f, this, tr( "Channel 4 Volume" ) ),
	
	m_ch4EnvEnabled( false, this ),
	m_ch4EnvLooped( false, this ),
	m_ch4EnvLen( 0.f, 0.f, 15.f, 1.f, this, tr( "Channel 4 Envelope length" ) ),
	
	m_ch4NoiseMode( false, this ),
	m_ch4NoiseFreqMode( false, this ),
	m_ch4NoiseFreq( 0.f, 0.f, 15.f, 1.f, this, tr( "Channel 4 Noise frequency" ) ), 
	
	//master
	m_masterVol( 1.0f, 0.0f, 2.0f, 0.01f, this, tr( "Master volume" ) ),
	m_vibrato( 0.0f, 0.0f, 15.0f, 1.0f, this, tr( "Vibrato (unimplemented)" ) )
{
	connect( &m_ch1Crs, SIGNAL( dataChanged() ), this, SLOT( updateFreq1() ) );
	connect( &m_ch2Crs, SIGNAL( dataChanged() ), this, SLOT( updateFreq2() ) );
	connect( &m_ch3Crs, SIGNAL( dataChanged() ), this, SLOT( updateFreq3() ) );
	
	updateFreq1();
	updateFreq2();
	updateFreq3();
}



NesInstrument::~NesInstrument()
{
}


void NesInstrument::playNote( NotePlayHandle * n, sampleFrame * workingBuffer )
{
	if ( n->totalFramesPlayed() == 0 || n->m_pluginData == NULL )
	{	
		NesObject * nes = new NesObject( this, engine::mixer()->processingSampleRate(), n, engine::mixer()->framesPerPeriod() );
		n->m_pluginData = nes;
	}
	
	const fpp_t frames = n->framesLeftForCurrentPeriod();
	
	NesObject * nes = static_cast<NesObject *>( n->m_pluginData );
	
	nes->renderOutput( workingBuffer, frames );
	
	applyRelease( workingBuffer, n );

	instrumentTrack()->processAudioBuffer( workingBuffer, frames, n );
}


void NesInstrument::deleteNotePluginData( NotePlayHandle * n )
{
	delete static_cast<NesObject *>( n->m_pluginData );
}


void NesInstrument::saveSettings(  QDomDocument & doc, QDomElement & element )
{
		m_ch1Enabled.saveSettings( doc, element, "on1" );
		m_ch1Crs.saveSettings( doc, element, "crs1" );
		m_ch1Volume.saveSettings( doc, element, "vol1" );
	
		m_ch1EnvEnabled.saveSettings( doc, element, "envon1" );
		m_ch1EnvLooped.saveSettings( doc, element, "envloop1" );
		m_ch1EnvLen.saveSettings( doc, element, "envlen1" );
	
		m_ch1DutyCycle.saveSettings( doc, element, "dc1" );
	
		m_ch1SweepEnabled.saveSettings( doc, element, "sweep1" );
		m_ch1SweepAmt.saveSettings( doc, element, "swamt1" );
		m_ch1SweepRate.saveSettings( doc, element, "swrate1" );
	
	// channel 2
		m_ch2Enabled.saveSettings( doc, element, "on2" );
		m_ch2Crs.saveSettings( doc, element, "crs2" );
		m_ch2Volume.saveSettings( doc, element, "vol2" );
	
		m_ch2EnvEnabled.saveSettings( doc, element, "envon2" );
		m_ch2EnvLooped.saveSettings( doc, element, "envloop2" );
		m_ch2EnvLen.saveSettings( doc, element, "envlen2" );
	
		m_ch2DutyCycle.saveSettings( doc, element, "dc2" );
	
		m_ch2SweepEnabled.saveSettings( doc, element, "sweep2" );
		m_ch2SweepAmt.saveSettings( doc, element, "swamt2" );
		m_ch2SweepRate.saveSettings( doc, element, "swrate2" );	
	
	//channel 3
		m_ch3Enabled.saveSettings( doc, element, "on3" );
		m_ch3Crs.saveSettings( doc, element, "crs3" );
		m_ch3Volume.saveSettings( doc, element, "vol3" );

	//channel 4
		m_ch4Enabled.saveSettings( doc, element, "on4" );
		m_ch4Volume.saveSettings( doc, element, "vol4" );
	
		m_ch4EnvEnabled.saveSettings( doc, element, "envon4" );
		m_ch4EnvLooped.saveSettings( doc, element, "envloop4" );
		m_ch4EnvLen.saveSettings( doc, element, "envlen4" );
	
		m_ch4NoiseMode.saveSettings( doc, element, "nmode4" );
		m_ch4NoiseFreqMode.saveSettings( doc, element, "nfrqmode4" );
		m_ch4NoiseFreq.saveSettings( doc, element, "nfreq4" );
	
	//master
		m_masterVol.saveSettings( doc, element, "vol" );
		m_vibrato.saveSettings( doc, element, "vibr" );	
}


void NesInstrument::loadSettings( const QDomElement & element )
{
		m_ch1Enabled.loadSettings(  element, "on1" );
		m_ch1Crs.loadSettings(  element, "crs1" );
		m_ch1Volume.loadSettings(  element, "vol1" );
	
		m_ch1EnvEnabled.loadSettings(  element, "envon1" );
		m_ch1EnvLooped.loadSettings(  element, "envloop1" );
		m_ch1EnvLen.loadSettings(  element, "envlen1" );
	
		m_ch1DutyCycle.loadSettings(  element, "dc1" );
	
		m_ch1SweepEnabled.loadSettings(  element, "sweep1" );
		m_ch1SweepAmt.loadSettings(  element, "swamt1" );
		m_ch1SweepRate.loadSettings(  element, "swrate1" );
	
	// channel 2
		m_ch2Enabled.loadSettings(  element, "on2" );
		m_ch2Crs.loadSettings(  element, "crs2" );
		m_ch2Volume.loadSettings(  element, "vol2" );
	
		m_ch2EnvEnabled.loadSettings(  element, "envon2" );
		m_ch2EnvLooped.loadSettings(  element, "envloop2" );
		m_ch2EnvLen.loadSettings(  element, "envlen2" );
	
		m_ch2DutyCycle.loadSettings(  element, "dc2" );
	
		m_ch2SweepEnabled.loadSettings(  element, "sweep2" );
		m_ch2SweepAmt.loadSettings(  element, "swamt2" );
		m_ch2SweepRate.loadSettings(  element, "swrate2" );	
	
	//channel 3
		m_ch3Enabled.loadSettings(  element, "on3" );
		m_ch3Crs.loadSettings(  element, "crs3" );
		m_ch3Volume.loadSettings(  element, "vol3" );

	//channel 4
		m_ch4Enabled.loadSettings(  element, "on4" );
		m_ch4Volume.loadSettings(  element, "vol4" );
	
		m_ch4EnvEnabled.loadSettings(  element, "envon4" );
		m_ch4EnvLooped.loadSettings(  element, "envloop4" );
		m_ch4EnvLen.loadSettings(  element, "envlen4" );
	
		m_ch4NoiseMode.loadSettings(  element, "nmode4" );
		m_ch4NoiseFreqMode.loadSettings(  element, "nfrqmode4" );
		m_ch4NoiseFreq.loadSettings(  element, "nfreq4" );
	
	//master
		m_masterVol.loadSettings(  element, "vol" );
		m_vibrato.loadSettings(  element, "vibr" );	
}	


QString NesInstrument::nodeName() const
{
	return( nes_plugin_descriptor.name );
}


PluginView * NesInstrument::instantiateView( QWidget * parent )
{
	return( new NesInstrumentView( this, parent ) );
}



void NesInstrument::updateFreq1()
{
	m_freq1 = powf( 2, m_ch1Crs.value() / 12.0f );
}


void NesInstrument::updateFreq2()
{
	m_freq2 = powf( 2, m_ch2Crs.value() / 12.0f );
}


void NesInstrument::updateFreq3()
{
	m_freq3 = powf( 2, m_ch3Crs.value() / 12.0f );
}




QPixmap * NesInstrumentView::s_artwork = NULL;


NesInstrumentView::NesInstrumentView( Instrument * instrument,	QWidget * parent ) :
	InstrumentView( instrument, parent )
{
	setAutoFillBackground( true );
	QPalette pal;

	if( s_artwork == NULL )
	{
		s_artwork = new QPixmap( PLUGIN_NAME::getIconPixmap( "artwork" ) );
	}

	pal.setBrush( backgroundRole(),	*s_artwork );
	setPalette( pal );

	const int KNOB_Y1 = 24;
	const int KNOB_Y2 = 81;
	const int KNOB_Y3 = 138;
	const int KNOB_Y4 = 195;
	
	const int KNOB_X1 = 12;
	const int KNOB_X2 = 46;
	const int KNOB_X3 = 84;
	const int KNOB_X4 = 117;
	const int KNOB_X5 = 151;
	const int KNOB_X6 = 172;
	const int KNOB_X7 = 206;
	
	// channel 1
	
	makeknob( m_ch1VolumeKnob, KNOB_X1, KNOB_Y1, "Volume", "", "" )
	makeknob( m_ch1CrsKnob, KNOB_X2, KNOB_Y1, "Coarse detune", "", "" )
	makeknob( m_ch1EnvLenKnob, KNOB_X3, KNOB_Y1, "Envelope length", "", "" )
	
	makenesled( m_ch1EnabledBtn, KNOB_X1, KNOB_Y1 - 12, "Enable channel 1" )
	makenesled( m_ch1EnvEnabledBtn, KNOB_X3, KNOB_Y1 - 12, "Enable envelope 1" )
	makenesled( m_ch1EnvLoopedBtn, 129, KNOB_Y1 - 12, "Enable envelope 1 loop" )

	makenesled( m_ch1SweepEnabledBtn, KNOB_X6, KNOB_Y1 - 12, "Enable sweep 1" )
	makeknob( m_ch1SweepAmtKnob, KNOB_X6, KNOB_Y1, "Sweep amount", "", "" )
	makeknob( m_ch1SweepRateKnob, KNOB_X7, KNOB_Y1, "Sweep rate", "", "" )

	int dcx = 117;
	makedcled( ch1_dc1, dcx, 42, "12.5% Duty cycle", "nesdc1_on" )
	dcx += 13;
	makedcled( ch1_dc2, dcx, 42, "25% Duty cycle", "nesdc2_on" )
	dcx += 13;
	makedcled( ch1_dc3, dcx, 42, "50% Duty cycle", "nesdc3_on" )
	dcx += 13;
	makedcled( ch1_dc4, dcx, 42, "75% Duty cycle", "nesdc4_on" )
		
	m_ch1DutyCycleGrp = new automatableButtonGroup( this );
	m_ch1DutyCycleGrp -> addButton( ch1_dc1 );
	m_ch1DutyCycleGrp -> addButton( ch1_dc2 );
	m_ch1DutyCycleGrp -> addButton( ch1_dc3 );
	m_ch1DutyCycleGrp -> addButton( ch1_dc4 );
	

	
	// channel 2
	
	makeknob( m_ch2VolumeKnob, KNOB_X1, KNOB_Y2, "Volume", "", "" )
	makeknob( m_ch2CrsKnob, KNOB_X2, KNOB_Y2, "Coarse detune", "", "" )
	makeknob( m_ch2EnvLenKnob, KNOB_X3, KNOB_Y2, "Envelope length", "", "" )
	
	makenesled( m_ch2EnabledBtn, KNOB_X1, KNOB_Y2 - 12, "Enable channel 2" )
	makenesled( m_ch2EnvEnabledBtn, KNOB_X3, KNOB_Y2 - 12, "Enable envelope 2" )
	makenesled( m_ch2EnvLoopedBtn, 129, KNOB_Y2 - 12, "Enable envelope 2 loop" )

	makenesled( m_ch2SweepEnabledBtn, KNOB_X6, KNOB_Y2 - 12, "Enable sweep 2" )
	makeknob( m_ch2SweepAmtKnob, KNOB_X6, KNOB_Y2, "Sweep amount", "", "" )
	makeknob( m_ch2SweepRateKnob, KNOB_X7, KNOB_Y2, "Sweep rate", "", "" )

	dcx = 117;
	makedcled( ch2_dc1, dcx, 99, "12.5% Duty cycle", "nesdc1_on" )
	dcx += 13;
	makedcled( ch2_dc2, dcx, 99, "25% Duty cycle", "nesdc2_on" )
	dcx += 13;
	makedcled( ch2_dc3, dcx, 99, "50% Duty cycle", "nesdc3_on" )
	dcx += 13;
	makedcled( ch2_dc4, dcx, 99, "75% Duty cycle", "nesdc4_on" )
		
	m_ch2DutyCycleGrp = new automatableButtonGroup( this );
	m_ch2DutyCycleGrp -> addButton( ch2_dc1 );
	m_ch2DutyCycleGrp -> addButton( ch2_dc2 );
	m_ch2DutyCycleGrp -> addButton( ch2_dc3 );
	m_ch2DutyCycleGrp -> addButton( ch2_dc4 );


	
	//channel 3
	makenesled( m_ch3EnabledBtn, KNOB_X1, KNOB_Y3 - 12, "Enable channel 3" )
	makeknob( m_ch3VolumeKnob, KNOB_X1, KNOB_Y3, "Volume", "", "" )
	makeknob( m_ch3CrsKnob, KNOB_X2, KNOB_Y3, "Coarse detune", "", "" )
	

	//channel 4
	makeknob( m_ch4VolumeKnob, KNOB_X1, KNOB_Y4, "Volume", "", "" )
	makeknob( m_ch4NoiseFreqKnob, KNOB_X2, KNOB_Y4, "Noise Frequency", "", "" )
	makeknob( m_ch4EnvLenKnob, KNOB_X3, KNOB_Y4, "Envelope length", "", "" )
	
	makenesled( m_ch4EnabledBtn, KNOB_X1, KNOB_Y4 - 12, "Enable channel 4" )
	makenesled( m_ch4EnvEnabledBtn, KNOB_X3, KNOB_Y4 - 12, "Enable envelope 4" )
	makenesled( m_ch4EnvLoopedBtn, 129, KNOB_Y4 - 12, "Enable envelope 4 loop" )

	makenesled( m_ch4NoiseModeBtn, 129, 203, "Noise mode" )
	makenesled( m_ch4NoiseFreqModeBtn,  129, 224, "Use note frequency for noise" )

	
	//master
	makeknob( m_masterVolKnob, KNOB_X4, KNOB_Y3, "Master Volume", "", "" )
	makeknob( m_vibratoKnob, KNOB_X5, KNOB_Y3, "Vibrato (unimplemented)", "", "" )

}



NesInstrumentView::~NesInstrumentView()
{
}


void NesInstrumentView::modelChanged()
{
	NesInstrument * nes = castModel<NesInstrument>();	

	m_ch1EnabledBtn->setModel( &nes->m_ch1Enabled );
	m_ch1CrsKnob->setModel( &nes->m_ch1Crs );
	m_ch1VolumeKnob->setModel( &nes->m_ch1Volume );

	m_ch1EnvEnabledBtn->setModel( &nes->m_ch1EnvEnabled );
	m_ch1EnvLoopedBtn->setModel( &nes->m_ch1EnvLooped );
	m_ch1EnvLenKnob->setModel( &nes->m_ch1EnvLen );

	m_ch1DutyCycleGrp->setModel( &nes->m_ch1DutyCycle );

	m_ch1SweepEnabledBtn->setModel( &nes->m_ch1SweepEnabled );
	m_ch1SweepAmtKnob->setModel( &nes->m_ch1SweepAmt );
	m_ch1SweepRateKnob->setModel( &nes->m_ch1SweepRate );

	// channel 2
	m_ch2EnabledBtn->setModel( &nes->m_ch2Enabled );
	m_ch2CrsKnob->setModel( &nes->m_ch2Crs );
	m_ch2VolumeKnob->setModel( &nes->m_ch2Volume );

	m_ch2EnvEnabledBtn->setModel( &nes->m_ch2EnvEnabled );
	m_ch2EnvLoopedBtn->setModel( &nes->m_ch2EnvLooped );
	m_ch2EnvLenKnob->setModel( &nes->m_ch2EnvLen );

	m_ch2DutyCycleGrp->setModel( &nes->m_ch2DutyCycle );

	m_ch2SweepEnabledBtn->setModel( &nes->m_ch2SweepEnabled );
	m_ch2SweepAmtKnob->setModel( &nes->m_ch2SweepAmt );
	m_ch2SweepRateKnob->setModel( &nes->m_ch2SweepRate );

	//channel 3
	m_ch3EnabledBtn->setModel( &nes->m_ch3Enabled );
	m_ch3CrsKnob->setModel( &nes->m_ch3Crs );
	m_ch3VolumeKnob->setModel( &nes->m_ch3Volume );

	//channel 4
	m_ch4EnabledBtn->setModel( &nes->m_ch4Enabled );
	m_ch4VolumeKnob->setModel( &nes->m_ch4Volume );

	m_ch4EnvEnabledBtn->setModel( &nes->m_ch4EnvEnabled );
	m_ch4EnvLoopedBtn->setModel( &nes->m_ch4EnvLooped );
	m_ch4EnvLenKnob->setModel( &nes->m_ch4EnvLen );

	m_ch4NoiseModeBtn->setModel( &nes->m_ch4NoiseMode );
	m_ch4NoiseFreqModeBtn->setModel( &nes->m_ch4NoiseFreqMode );
	m_ch4NoiseFreqKnob->setModel( &nes->m_ch4NoiseFreq );

	//master
	m_masterVolKnob->setModel( &nes->m_masterVol );
	m_vibratoKnob->setModel( &nes->m_vibrato );		
}


extern "C"
{

// necessary for getting instance out of shared lib
Plugin * PLUGIN_EXPORT lmms_plugin_main( Model *, void * _data )
{
	return( new NesInstrument( static_cast<InstrumentTrack *>( _data ) ) );
}


}


#include "moc_Nes.cxx"