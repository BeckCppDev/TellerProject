void setup();
void loop();

int main()
{
  setup();
  // 終了は原則Teller::quit()で行うこと
  for (;;)
  {
    loop();
  }
}
