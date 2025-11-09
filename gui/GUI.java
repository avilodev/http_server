import java.awt.FlowLayout;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import javax.swing.JButton;
import javax.swing.JFrame;

public class GUI
{
    private static long serverPid;
    private static Process serverProcess;
    public static void main(String[] args) 
    {

        JFrame window = new JFrame("Server");
        window.setLayout(new FlowLayout());

        JButton startButton = new JButton("Start Server");
        window.add(startButton);
        startButton.addActionListener(e -> {
            System.out.println("Start button was clicked.");

            if(serverPid > 0)
            {
                System.out.println("Server is already Running!");
                return;
            }

            try
            {
                Runtime r = Runtime.getRuntime();
                serverProcess = r.exec("sudo ../server");

                Process getPidProcess = Runtime.getRuntime().exec("sudo lsof | grep 'server' | awk '{print $2}'");
                getPidProcess.waitFor();

                BufferedReader pidReader = new BufferedReader(new InputStreamReader(getPidProcess.getInputStream()));

                boolean signal = true;
                String pidStr = "0";
                String holder = pidReader.readLine();
                while(signal)
                {
                    pidStr = holder;
                    holder = pidReader.readLine();
                    if(pidStr == null)
                        signal = false;
                }

                if (pidStr != null && !pidStr.isEmpty()) {
                    serverPid = Long.parseLong(pidStr.trim());
                }

                pidReader.close();

                System.out.println("Captured Server PID: " + pidStr);
                serverPid = Long.parseLong(pidStr);

                new Thread(() -> {
                    try (BufferedReader reader = new BufferedReader(new InputStreamReader(serverProcess.getInputStream()))) {
                        String line;
                        while ((line = reader.readLine()) != null) {
                            System.out.println(line);
                        }
                    } catch (Exception ex) {
                        System.out.println("Error reading output: " + ex);
                    }
                }).start();
            }
            catch (Exception ex)
            {
                System.out.println(ex.toString());
            }

        });

        JButton killButton = new JButton("Kill Server");
        window.add(killButton);

        killButton.addActionListener(e -> {
            System.out.println("Kill button was clicked.");

            if(serverPid == 0)
            {
                System.out.println("Server is not running!");
                return;
            }

            try
            {
                System.out.println("Trying to Kill\nsudo kill -10 " + serverPid);
                Runtime r = Runtime.getRuntime();
                Process p = r.exec("sudo kill -10 " + serverPid);
                p.waitFor();

                System.out.println("Successfully Killed");
                serverPid = 0;
            }
            catch (Exception execption)
            {
                System.out.println(execption.toString());
            }

        });

        window.setSize(400, 300);
        window.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        window.setVisible(true);
    }
}
