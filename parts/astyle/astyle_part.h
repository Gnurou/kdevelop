/*
 *  Copyright (C) 2001 Matthias H�lzer-Kl�pfel <mhk@caldera.de>   
 */
 

#ifndef __KDEVPART_ASTYLE_H__
#define __KDEVPART_ASTYLE_H__


#include <kaction.h>


#include <kdevpart.h>


class AStylePart : public KDevPart
{
  Q_OBJECT

public:
   
  AStylePart(KDevApi *api, QObject *parent=0, const char *name=0);
  ~AStylePart();


private slots:

  void activePartChanged(KParts::Part *newPart);

  void beautifySource();
 
  void configWidget(KDialogBase *dlg);


private:

  KAction *_action;

};


#endif
