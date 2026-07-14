// -*-c++-*-
// ver rival_tuning.h

#include "rival_tuning.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

RivalTuning &
RivalTuning::i()
{
    static RivalTuning s_instance;
    return s_instance;
}

void
RivalTuning::update( const rcsc::WorldModel & wm )
{
    if ( M_loaded ) return;

    const std::string & opp = wm.theirTeamName();
    if ( opp.empty() ) return;  // aún no conocemos al rival

    M_loaded = true;  // una sola pasada, matchee o no

    std::ifstream f( "./rival_tuning.conf" );
    if ( ! f.good() ) return;  // sin archivo = defaults

    std::string line;
    while ( std::getline( f, line ) )
    {
        if ( line.empty() || line[0] == '#' ) continue;
        std::istringstream ss( line );
        std::string key, knob;
        double value;
        if ( ! ( ss >> key >> knob >> value ) ) continue;
        if ( opp.find( key ) == std::string::npos ) continue;

        if      ( knob == "pressing" )     M_press_base   = static_cast<int>( value );
        else if ( knob == "deep_press_x" ) M_deep_press_x = value;
        else if ( knob == "wing_x" )       M_wing_x       = value;
        else if ( knob == "wing_y" )       M_wing_y       = value;
        else
        {
            std::cerr << "[RivalTuning] perilla desconocida: " << knob << std::endl;
            continue;
        }
        std::cerr << "[RivalTuning] " << opp << " matchea '" << key
                  << "': " << knob << " = " << value << std::endl;
    }
}
