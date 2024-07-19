#include <cdio/cdio.h>
#include <cdio/cd_types.h>
#include <cdio/audio.h>
#include <cdio/track.h>
#include <ao/ao.h>
#include <iostream>
#include <memory>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <iomanip>
#include <cstring>


#define BUFFER_SIZE 2352 // CD audio sector size

void print_time(int seconds) {
    int minutes = seconds / 60;
    seconds = seconds % 60;
    std::cout << std::setw(2) << std::setfill('0') << minutes << ":"
              << std::setw(2) << std::setfill('0') << seconds << "\r";
    std::cout.flush();
}

void hide_cursor() {
    std::cout << "\033[?25l";
    std::cout.flush();
}

void show_cursor() {
    std::cout.flush();

    std::cout << "\033[?25h";  // Show cursor
    std::cout << "\033[0m";    // Reset all ANSI attributes to default
    std::cout.flush();


    // Reset terminal attributes to default
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag |= ECHO | ICANON;  // Enable echo and canonical mode
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
    std::cout.flush();
}


int main()
{
    struct termios term;
    tcgetattr(fileno(stdin), &term);

    term.c_lflag &= ~ECHO;
    tcsetattr(fileno(stdin), 0, &term);


    // Initialize libcdio
    CdIo_t *cdio = cdio_open(NULL, DRIVER_DEVICE);
    if (cdio == NULL)
    {
        std::cerr << "Failed to open CD drive." << std::endl;
        return 1;
    }

    // Get the first track
    const auto total_tracks_number = cdio_get_num_tracks(cdio);
    std::cout << "Number of tracks: " << (int) total_tracks_number << "\n";

    track_t first_track = cdio_get_first_track_num(cdio);

    if (first_track == CDIO_INVALID_TRACK)
    {
        std::cerr << "Failed to get the first track number." << std::endl;
        cdio_destroy(cdio);
        return 1;
    }

    // Initialize libao
    ao_initialize();
    int driver = ao_default_driver_id();
    ao_sample_format format;
    memset(&format, 0, sizeof(format));
    format.bits = 16;
    format.channels = 2;
    format.rate = 44100;
    format.byte_format = AO_FMT_NATIVE;
    ao_device *device = ao_open_live(driver, &format, NULL);
    if (device == NULL)
    {
        std::cerr << "Failed to open audio device." << std::endl;
        cdio_destroy(cdio);
        ao_shutdown();
        return 1;
    }

    hide_cursor();

    char input_command{};
    fcntl(STDIN_FILENO, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    // Read and play the first track
    for(uint8_t track_index = 1; track_index <= total_tracks_number; track_index++)
    {
        std::cout << "\nPlaying track: " << (int) track_index << '\n';

        lsn_t lsn_start = cdio_get_track_lsn(cdio, track_index);
        lsn_t lsn_end = cdio_get_track_last_lsn(cdio, track_index);
        uint8_t buffer[BUFFER_SIZE];

        int total_seconds = (lsn_end - lsn_start + 1) / 75;
        int elapsed_seconds = 0;

        std::cout << "Total time: ";
        print_time(total_seconds);
        std::cout << std::endl;

        msf_t track_msf{};
        cdio_get_track_msf(cdio, track_index, &track_msf);
        char * trac_msf_str = cdio_msf_to_str(&track_msf);
        std::cout << "Track MSF: " << trac_msf_str << "\n";
        free(trac_msf_str);


        for (lsn_t lsn = lsn_start; lsn <= lsn_end; lsn++)
        {
            if (cdio_read_audio_sector(cdio, buffer, lsn) != DRIVER_OP_SUCCESS)
            {
                std::cerr << "Failed to read audio sector at LSN " << lsn
                        << std::endl;
                break;
            }

            ao_play(device, (char*) buffer, BUFFER_SIZE);

            // Update and print elapsed time
            if ((lsn - lsn_start) % 75 == 0) {
                elapsed_seconds = (lsn - lsn_start) / 75;
                std::cout << "Elapsed time: ";
                print_time(elapsed_seconds);
            }

            bool has_input = read(STDIN_FILENO, &input_command, 1) > 0;
            if(has_input)
            {
                bool interrupt_current_track = true;

                switch(tolower(input_command))
                {
                    case 's':
                    {
                        track_index = 254;
                        break;
                    }
                    case 'p':
                    {
                        if(track_index == 1)
                        {
                            track_index = (total_tracks_number - 1);
                        }
                        else
                        {
                            track_index -= 2;
                        }

                        break;
                    }
                    case 'n':
                    {
                        if((track_index + 1) > total_tracks_number)
                        {
                            track_index = 0;
                        }
                        break;
                    }
                    default:
                    {
                        interrupt_current_track = false;
                        break;
                    }
                }

                tcflush(STDIN_FILENO, TCIFLUSH);

                if(interrupt_current_track)
                {
                    break;
                }
            }

        }

    }

    // Cleanup
    ao_close(device);
    ao_shutdown();
    cdio_destroy(cdio);
    show_cursor();
    return 0;
}
