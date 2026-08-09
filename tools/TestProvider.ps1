$pipeName = 'IGCSDOF_UniversalBridge_v1'
$pipe = [System.IO.Pipes.NamedPipeClientStream]::new('.', $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
$pipe.Connect(5000)
$reader = [System.IO.StreamReader]::new($pipe)
$writer = [System.IO.StreamWriter]::new($pipe); $writer.AutoFlush = $true
$writer.WriteLine('HELLO|Protocol=1|Provider=PowerShell Test|Game=Bridge Test|Engine=Not Available|EngineVersion=')
$writer.WriteLine('CAMERA|1|1|0|10|20|30|0|0|0|1|1|0|0|0|1|0|0|0|1|0|0|0|70')
Write-Host ('Bridge says: ' + $reader.ReadLine())
Write-Host 'Listening. Start an IGCS test render or press Ctrl+C.'
while ($pipe.IsConnected) {
  $line = $reader.ReadLine(); if ($null -eq $line) { break }
  Write-Host ('COMMAND: ' + $line)
  if ($line.StartsWith('SESSION_BEGIN')) { $writer.WriteLine('ACK|SESSION_BEGIN') }
  elseif ($line.StartsWith('SESSION_END')) { $writer.WriteLine('ACK|SESSION_END') }
}
