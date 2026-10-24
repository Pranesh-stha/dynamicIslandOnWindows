#pragma once

// "Sleep" (header button): the same sleep as Start › Power › Sleep. The
// screen and keyboard light go off and the fans stop; a key press wakes the
// PC. It locks first, so waking asks for your PIN, password or face.
namespace SystemSleep
{
    bool Now();  // false if Windows refused (logged)
}
