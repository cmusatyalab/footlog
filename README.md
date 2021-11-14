# footlog
Code for logging up/down events from foot pedal.  This is useful for experiments involving subjective user experience.  The user presses down on the pedal during periods of bad experience.  The hands remain free for keyboard and mouse interactions.  The foot pedal device is disabled from X windows, and so does not provide confusing inputs to Linux, X or the application.  Timestamped down and up events are recorded in /var/log/footlog/events.log.  Later, in post-processing, the timestamps in this log file can be correlated with other timestamped information such as system-level or application-level counters or signals of various kinds.

The initial code works only with a specific foot pedal, made by QinHeng Electronics and available on Amazon (in November 2021) under the following description:

     "iKKEGOL USB Single Foot Pedal Optical Switch Control
     One Key Program Computer Keyboard Mouse Game Action HID" 
Extension to other food pedals should be straightforward, just by changing the string used for search of USB devices in file usbstuff.c.   The foot pedal should be set to continuously emit the ASCII character "1" (digit one) when pressed, nothing else.
