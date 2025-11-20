Project 1:
CSCI4760 (Undergraduate)
Name: Igor Rodrigues Goncalves
UGA#: 811 363 160

Project Structure:

Interprocess Communication:

Crash Handling:
- In this program each child process has an independently random chance to crash based on the crash rate input via command line argument 3.
- This is implemented with the abort() command after the word count function.
- The parent uses waitpid() to monitor the exit status of each chil.
- When a crash is detected, the parent respawns a new child for the same chunk of the file.
- This loop continues until all chunks are successfully processed.
